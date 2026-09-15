#include "Interactive/Widgets/ArrowRotateWidget.h"
#include "Interactive/Im3DRenderer.h"
#include "Interactive/MathUtil/MathUtil.h"
#include "Core/Global/ServiceLocator.h"
#include "Rendering/Resources/Mesh.h"
#include "Rendering/Resources/Model.h"
#include "renderer/Context.h"
#include "Interactive/GizmoBehaviour.h"
#include "renderer/SceneView.h"
#include "Interactive/ViewData.h"
#include <cmath>
namespace MOON {
		// Same FIXED_SCALE ratio as GizmoCell.ovfx (see ClipPlane.cpp for the full
	// derivation): the widget is scaled by ratio / refRatio so it keeps a
	// constant screen size as the camera moves. The reference ratio is captured
	// on the first frame, locking the gizmo to the size it had when it appeared.
	static float ComputeFixedScaleRatio(
		Editor::Panels::SceneView& p_view,
		const Eigen::Vector3f& p_worldPos)
	{
		const auto* camera = p_view.GetCamera();
		if (!camera) return 1.0f;

		const auto& projection = camera->GetProjectionMatrix();
		const float proj11 = projection(1, 1);
		const float screenHeight = static_cast<float>(p_view.GetRenderer().GetFrameDescriptor().renderHeight);
		if (proj11 <= 0.0f || screenHeight <= 0.0f) return 1.0f;

		if (camera->GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC)
		{
			return 400.0f / (proj11 * screenHeight);
		}

		const Maths::FVector3 viewPos = camera->GetViewMatrix().MulPoint(
			Maths::FVector3{ p_worldPos.x(), p_worldPos.y(), p_worldPos.z() });
		const float depth = std::abs(viewPos.z);
		return 400.0f * depth / (proj11 * screenHeight);
	}
class ArrowRotateWidget::Internal {
	public:
		Internal(ArrowRotateWidget*s):self(s){}
		~Internal(){}
	private:
		friend ArrowRotateWidget;
		ArrowRotateWidget* self = nullptr;
		Eigen::Vector3f rotateCenter = {0,0,0};
		Eigen::Vector3f rotateAxis = { 1,0,0 };
		Eigen::Vector3f originPos = {0,0,0};
		Eigen::Vector3f curPos = {0,0,0};
		
		WidgetViewData viewData;
		GizmoAxisRotate rotatePick;
		
		std::string curHitTarget = "";
			float mRefScaleRatio = -1.0f;
		float mLastScale = 1.0f;
	};
	ArrowRotateWidget::ArrowRotateWidget(const std::string& name):EventWidget(name), mInternal(new Internal(this)),mState(Stop)
	{
		std::vector<Eigen::Vector3f>f;
		Maths::FMatrix4 matrix =Maths::FMatrix4::Translation({ 0,0,0 }) * Maths::FMatrix4::Scaling({ 10,10,10 });
		auto model = GetModelService[":Models/Arrow_Translate.fbx"];
		for (auto mesh : model->GetMeshes()) {
			int vcnt = mesh->GetVertexCount();
			int icnt = mesh->GetIndexCount();
			int num = icnt > 0 ? icnt : vcnt;
			for (int i = 0; i < num; i += 3) {
				auto v0 = Maths::FMatrix4::MulPoint(matrix, mesh->GetVertexPosition(i));
				auto v1 = Maths::FMatrix4::MulPoint(matrix, mesh->GetVertexPosition(i + 1));
				auto v2 = Maths::FMatrix4::MulPoint(matrix, mesh->GetVertexPosition(i + 2));
				f.push_back(Eigen::Vector3f(v0.x,v0.y,v0.z));
				f.push_back(Eigen::Vector3f(v1.x, v1.y, v1.z));
				f.push_back(Eigen::Vector3f(v2.x, v2.y, v2.z));
			}
		}
		mInternal->viewData.setTriangleFace("Arrow", f, {255,255,255,0});
		setActive(true);
	}
	ArrowRotateWidget::~ArrowRotateWidget()
	{
		delete mInternal;
	}
	void ArrowRotateWidget::onUpdate()
	{
		const float ratio = ComputeFixedScaleRatio(*m_sceneView, mInternal->rotateCenter);
		if (mInternal->mRefScaleRatio < 0.0f)
		{
			// Lock the gizmo to its current screen size; afterwards it stays
			// constant no matter how the camera moves.
			mInternal->mRefScaleRatio = ratio;
		}
		const float scale = mInternal->mRefScaleRatio > 0.0f ? ratio / mInternal->mRefScaleRatio : 1.0f;
		mInternal->mLastScale = scale;

		Eigen::Matrix4f scaleMat = Eigen::Matrix4f::Identity();
		scaleMat(0, 0) = scaleMat(1, 1) = scaleMat(2, 2) = scale;

		const auto& faces = mInternal->viewData.getFaces();
		for (int i = 0; i < faces.size(); i++) {
			renderer->pushMatrix(faces[i].model * scaleMat);
			renderer->drawTriangleList(faces[i].faces, 1.0, faces[i].color);
			renderer->popMatrix();
		}
		renderer->drawPoint(mInternal->rotateCenter,20, Eigen::Vector4<uint8_t>(255,0,255,255));
		renderer->pushSize(6);
		renderer->drawLine(mInternal->rotateCenter, mInternal->rotateCenter + mInternal->rotateAxis * (10.0f * scale));
		renderer->popSize();
	}
	void ArrowRotateWidget::setUpRotateCenter(float x, float y, float z)
	{
		mInternal->rotateCenter = Eigen::Vector3f(x, y, z);
	}
	void ArrowRotateWidget::setUpRotateAxis(float x, float y, float z)
	{
		mInternal->rotateAxis = Eigen::Vector3f(x,y,z).normalized();
		TriangleFace* face;
		if (mInternal->viewData.getTriangleFace("Arrow", face)) {
			Eigen::Vector3f offset = mInternal->curPos - mInternal->rotateCenter;
			Eigen::Vector3f arrowDir = mInternal->rotateAxis.cross(offset).normalized();
			face->model.block(0, 0, 3, 3) = RotationMatrixZ(arrowDir);
		}
	}

	void ArrowRotateWidget::setUpOriginPos(float x, float y, float z)
	{
		mInternal->originPos = Eigen::Vector3f(x,y,z);
		TriangleFace* face;
		if (mInternal->viewData.getTriangleFace("Arrow", face)) {
			face->model.block(0, 3, 3, 1) = mInternal->originPos;
		}
		mInternal->curPos = mInternal->originPos;
	}

	void ArrowRotateWidget::setUpScale(float s)
	{
		mInternal->viewData.setTriangleFaceScale("Arrow",s);
	}


	void ArrowRotateWidget::setAngle(float angleDeg)
	{
		// 角度转弧度
		float rad = angleDeg * 3.14159265358979323846f / 180.0f;

		auto& center = mInternal->rotateCenter;
		auto& axis = mInternal->rotateAxis;
		auto& origin = mInternal->originPos;

		// 1. 基准径向向量（垂直旋转轴分量）
		Eigen::Vector3f originVec = origin - center;
		float proj = originVec.dot(axis);
		Eigen::Vector3f radial0 = originVec - proj * axis;

		// 2. 罗德里格斯：绕轴旋转 radial0 得到旋转后径向向量
		Eigen::Matrix3f rotMat;
		float c = cos(rad);
		float s = sin(rad);
		float oneMinusC = 1.0f - c;
		Eigen::Matrix3f K;
		K << 0, -axis.z(), axis.y(),
			axis.z(), 0, -axis.x(),
			-axis.y(), axis.x(), 0;
		rotMat = Eigen::Matrix3f::Identity() + s * K + oneMinusC * K * K;

		Eigen::Vector3f radialRot = rotMat * radial0;

		// 3. 还原完整坐标（保留平行轴分量）
		Eigen::Vector3f newPos = center + radialRot + proj * axis;
		mInternal->curPos = newPos;

		// 4. 更新箭头面片平移与朝向矩阵
		TriangleFace* face = nullptr;
		if (mInternal->viewData.getTriangleFace("Arrow", face))
		{
			// 平移到新位置
			face->model.block(0, 3, 3, 1) = mInternal->curPos;

			// 重新计算箭头朝向
			Eigen::Vector3f offset = mInternal->curPos - mInternal->rotateCenter;
			Eigen::Vector3f arrowDir = mInternal->rotateAxis.cross(offset).normalized();
			face->model.block(0, 0, 3, 3) = RotationMatrixZ(arrowDir);
		}
	}

	float ArrowRotateWidget::getAngle()
	{
		Eigen::Vector3f a=(mInternal->originPos - mInternal->rotateCenter);
		a = a - a.dot(mInternal->rotateAxis) * mInternal->rotateAxis;
		Eigen::Vector3f b = (mInternal->curPos - mInternal->rotateCenter);
		b = b - b.dot(mInternal->rotateAxis) * mInternal->rotateAxis;
		a = a.normalized();
		b = b.normalized();
		float val=a.cross(b).dot(mInternal->rotateAxis);
		float angle = acos(a.dot(b))/ 3.14159265358979323846f * 180.0f;
		return  val>0.0?angle:(360.0f- angle) ;
	}
	void ArrowRotateWidget::onLeftMousePressed()
	{
		if(mState==Hot) {
			if (mInternal->curHitTarget == "Arrow") {
				mState = Rotate;
				mInternal->viewData.setTriangleFace("Arrow", Eigen::Vector4<uint8_t>(255, 0, 255, 255));
				Eigen::Vector3f dir = { 0,1,0 };
				mInternal->rotatePick.startPick(mInternal->rotateAxis, mInternal->rotateCenter,dir,mInternal->curPos);
			}
		}
	}
	void ArrowRotateWidget::onLeftMouseReleased()
	{
		if (mState == Rotate ) {
			mState = Hot;
			mInternal->viewData.setTriangleFace("Arrow", Eigen::Vector4<uint8_t>(255, 255, 255, 0));
			if (!mImInvoke) {
				this->InvokeEvent(ArrowRotateEvent::AngleChange);
			}
		}
	}
	void ArrowRotateWidget::onMouseMove()
	{	
		if (mState == Stop) {	
			auto ray=m_sceneView->GetMouseRay();
			Ray it(Eigen::Vector3f(ray.origin_.x, ray.origin_.y, ray.origin_.z), Eigen::Vector3f(ray.direction_.x, ray.direction_.y, ray.direction_.z));
			mInternal->curHitTarget =mInternal->viewData.hitFace(it, mInternal->mLastScale);
			if (mInternal->curHitTarget == "Arrow") {
				mState = Hot;
				mInternal->viewData.setTriangleFace("Arrow", Eigen::Vector4<uint8_t>(255,0,0,255));
			}
		}
		else if (mState == Hot) {
			
			auto ray = m_sceneView->GetMouseRay();
			Ray it(Eigen::Vector3f(ray.origin_.x, ray.origin_.y, ray.origin_.z), Eigen::Vector3f(ray.direction_.x, ray.direction_.y, ray.direction_.z));
			mInternal->curHitTarget = mInternal->viewData.hitFace(it, mInternal->mLastScale);
			if (mInternal->curHitTarget == "") {
				mState = Stop;
				mInternal->viewData.setTriangleFace("Arrow", Eigen::Vector4<uint8_t>(255, 255, 255, 0));
			}
		}
		else if (mState==Rotate) {
			auto& param = renderer->getFrameParam();
			mInternal->rotatePick.applyPos(param.rayDirection, param.rayOrigin, mInternal->curPos);
			TriangleFace* face;
			if (mInternal->viewData.getTriangleFace("Arrow", face)) {
				face->model.block(0, 3, 3, 1) = mInternal->curPos;
				Eigen::Vector3f offset= mInternal->curPos - mInternal->rotateCenter;
				Eigen::Vector3f arrowDir=mInternal->rotateAxis.cross(offset);
				face->model.block(0, 0, 3, 3) = RotationMatrixZ(arrowDir);
			}
			if (mImInvoke) {
				this->InvokeEvent(ArrowRotateEvent::AngleChange);
			}
		}
	}
}