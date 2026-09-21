#pragma once
#include <math.h>
#include <Eigen/Core>
#include <vector>
#include <unordered_map>
#include "Interactive/Im3DType.h"
#include "Rendering/Resources/Texture.h"
namespace Editor {
	namespace Panels {
		class SceneView;
	}
}
namespace Rendering::Data
{
	class Material;
}
namespace MOON
{
	using Vec2 = Eigen::Vector2f;
	class EventWidget;
	
	class ImRenderer
	{
	private:
		ImRenderer();
		~ImRenderer();
		ImRenderer(const ImRenderer&) = delete;
		ImRenderer& operator=(const ImRenderer&) = delete;
	public:
		static ImRenderer& instance();
		void init();
		void preStoreMesh();
		void prepareGl();
		void registerDebugSettings();
		void terminate();
		void selectPolygon(int pid,int bid);
		bool isSelectPolygon(const std::string&pname,const std::string& bname);
		void resetSelectPolygon();
		void begin(PrimitiveMode _mode);
		void end();
		void vertex(const Eigen::Vector3f& p, const Eigen::Vector4f& c, const Eigen::Vector3f& n, const Eigen::Vector2f& tex);
		void vertex(const Eigen::Vector3f& p, const Eigen::Vector4<uint8_t>& c, const Eigen::Vector3f& n, const Eigen::Vector2f& tex);
		void vertex(const Eigen::Vector3f& _position, float _size, const Eigen::Vector4<uint8_t>& _color);
		void vertex(const Eigen::Vector3f& _position)
		{
			vertex(_position, sizeStack.back(), colorStack.back());
		}
		void vertex(const Eigen::Vector3f& _position, Eigen::Vector4<uint8_t> _color)
		{
			vertex(_position, sizeStack.back(), _color);
		}
		//void draw
		void drawOneMesh(const Eigen::Vector3f& translation, const Eigen::Matrix3f& rotation, const Eigen::Vector3f& scale, const std::string& mesh, bool longterm = false);
		void drawOneMesh(Eigen::Vector3f& translation, Eigen::Matrix3f& rotation, Eigen::Vector3f& scale,
			Eigen::Vector3f& color,
			const std::string& mesh, bool longterm = false);
		void drawOneMesh(
			Eigen::Vector3f translation, Eigen::Vector3f scale, const std::string& mesh, bool longterm = false);
		void drawOneMesh(
			Eigen::Vector3f translation, Eigen::Vector3f scale, Eigen::Vector3f& color, const std::string& mesh, bool longterm = false);
		void drawOneMesh(const Eigen::Matrix4f& model, const std::string& mesh, bool longterm = false);
		void drawOneMesh(const Eigen::Matrix4f& model, const std::string& mesh, Eigen::Vector3f& color, bool longterm = false);
		void drawOneFixScaleMesh(
			Eigen::Matrix4f& model, const std::string& mesh, Eigen::Vector3f& color, bool longterm = false);
		void drawPoint(const Eigen::Vector3f& _position, float _size, Eigen::Vector4<uint8_t> _color);
		void drawPoint(const Eigen::Vector3f& pos);
		void drawPoint(const Eigen::Vector3f& pos, float size);
		void drawPointList(const std::vector<Eigen::Vector3f>& position, float size, Eigen::Vector4<uint8_t> color);
		void drawLineList(const std::vector<Eigen::Vector3f>& position, float _size, Eigen::Vector4<uint8_t> _color);
		void drawTriangleList(const std::vector<Eigen::Vector3f>& position, float _size, Eigen::Vector4<uint8_t> _color);
		void drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, float _size, Eigen::Vector4<uint8_t> _color);
		void drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, float _size);
		void drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b);
		
		void drawTriangle(const Eigen::Vector3f& a, const Eigen::Vector3f& b, const Eigen::Vector3f& c, const Eigen::Vector3f& n);
		void drawTriangle(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c);
		void drawTriangle(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c, const Eigen::Vector4<uint8_t>& _color);
		void drawQuadFilled(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, const Eigen::Vector2<float>& _size);
		void drawQuad(
			const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c, const Eigen::Vector3f& _d);
		void drawQuad(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, const Eigen::Vector2<float>& _size);
		void drawQuadFilled(
			const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c, const Eigen::Vector3f& _d);
		void drawArrow(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _headLength, float axisThickness,
			float _headThickness);
		void drawArrow(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _headLength = -1.0f,
			float _headThickness = -1.0f);
		void drawArrow(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldHeight,
			Eigen::Vector4<uint8_t> _color);
		void drawCircleFilled(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail = -1);
		void drawTransparencyCircle(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail = -1);
		void drawSphere(const Eigen::Vector3f& _origin, float _radius, int _detail = -1);
		void drawSphereFilled(const Eigen::Vector3f& _origin, float _radius, int _detail = -1);
		void drawCircleFaceCamera(const Eigen::Vector3f& _origin);
		void drawCircle(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail = -1);
		
		void drawConeFilled(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float height, float _radius, int _detail);
		void drawAlignedBox(const Eigen::Vector3f& _min, const Eigen::Vector3f& _max);
		void drawAlignedBoxFilled(const Eigen::Vector3f& _min, const Eigen::Vector3f& _max);
		void drawAlignedBoxFilled(
			const Eigen::Vector3f& _min, const Eigen::Vector3f& _max, const std::vector<Eigen::Vector4<uint8_t>>& color);
		void drawPlaneGrid(const Eigen::Vector3f& origin, const Eigen::Vector3f& normal, float scale, float size);
		bool drawManpulate(unsigned _id, Eigen::Matrix4f& model);
		bool drawManpulate(const char* _id, Eigen::Matrix4f& model);

		void drawRayHitScreenPoint();
		bool lineEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, Eigen::Vector4<uint8_t> _color);
		bool scaleEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, Eigen::Vector4<uint8_t> _color, int& index);
		bool planeEdit(unsigned int id, Eigen::Vector3f& origin, Eigen::Vector3f& normal);
		bool planeEdit(unsigned int id, std::vector<Eigen::Vector3f>& threePoints, int& index);
		bool axisRotateEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, float& angle);
		bool translation(unsigned int _id, Eigen::Vector3f& _translation_, bool _local = false);
		bool gizmoPlaneTranslationBehavior(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _snap,
			float _worldSize, Eigen::Vector3f* _out_);
		void gizmoAxisTranslationDraw(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis,
			float _worldHeight, float _worldSize, Eigen::Vector4<uint8_t> _color);
		bool gizmoAxisTranslationBehavior(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis,
			float _snap, float _worldHeight, float _worldSize, Eigen::Vector3f* _out_);
		void gizmoPlaneTranslationDraw(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal,
			float _worldSize, Eigen::Vector4<uint8_t> _color);
		bool gizmoPlaneTranslationBehavior(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal,
			float _snap, float _worldSize, Eigen::Vector3f* _out_);
		bool rotation(unsigned int _id, Eigen::Matrix3f& _rotation_, bool _local = false);
		bool gizmoAxislAngleBehavior(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _snap, float _worldRadius, float _worldSize, float* _out_);
		void axisRotateDraw(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldRadius, Eigen::Vector4<uint8_t> _color);
		void gizmoAxislAngleDraw(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldRadius, float _angle, Eigen::Vector4<uint8_t> _color, float _minAlpha);
		int  estimateLevelOfDetail(const Eigen::Vector3f& _position, float _worldSize, int _min = 4, int _max = 256);
		bool gizmoSpherePlaneTranslationBehavior(unsigned _id, const Eigen::Vector3 < float >& _origin, float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_);
		bool gizmoCircleAxisTranslationBehavior(unsigned int _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_);
		bool gizmoSphereRotateInCircleBehavior(unsigned int _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& _normal, Eigen::Vector3f* _out_,bool inPlane=true);
		bool gizmoSphereAxisTranslationBehavior(unsigned int _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_);
		bool gizmoOperateNormalBehavior(unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _end, float _worldSize, Eigen::Vector3f* _out_);
		bool planeClip(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Vector3f& _normal, float& _scale, const Eigen::Vector3f& minBox, const Eigen::Vector3f& maxBox, bool opeartorNormal, bool drawCircle);
		bool boxEdit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Matrix3f& _rotation_, Eigen::Vector3f& _scale_);
		bool boxEdit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Matrix3f& _rotation_, Eigen::Vector3f& _scale_, bool translationFlag, bool rotateFlag, bool faceMoveFlag, int& mode);
		bool sphereEdit(unsigned int _id, Eigen::Vector3f& _translation_, float& r);
		bool dragPointInPlane(unsigned int id, Eigen::Vector3f& translation, const Eigen::Vector3f& normal);
		bool dragPointByAxis(unsigned int id, Eigen::Vector3f& translation, const Eigen::Vector3f& axis);
		bool clydinerEdit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Vector3f& _normal, float& height,
			float& _tr, float& _br, bool same = true);
		bool PrismEdit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin, float* _height,
			std::vector<Eigen::Vector3f>& polygon, std::vector<Eigen::Vector2f>& polygon2D, bool flip = false);
		bool CurveEdit(
			unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin, std::vector<Eigen::Vector3f>& polygon);
		bool CurveEdit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin,
			std::vector<std::pair<Eigen::Vector3f, Eigen::Vector3f>>& polygon);
		//methods drawing in screenspace!
		bool drawTranslate2D(unsigned int id, Eigen::Vector2f& pos);
		bool drawLineSplit(unsigned int id, Eigen::Vector2f& a, Eigen::Vector2f& b);
		Eigen::Vector2f worldToScreen(const Eigen::Vector3f& pos);
		FrameParam& getFrameParam();
		void test();
		void drawUnsort();
		void drawSort();
		void drawMesh();
		void drawMeshPick();
		void newFrame(Editor::Panels::SceneView* view);
		void endFrame();
		void newImgui();
		void endImgui();
		void clear();
		void updateDepth(float _depth);
		bool makeHot(unsigned int _id, float _depth, bool _intersects);
		bool makeHot2D(unsigned int id);
		bool isHot(unsigned int _id);
		bool isHot2D(unsigned int id);
		bool isAppActive(unsigned int _id);
		void makeActive(unsigned int _id);
		void makeActive2D(unsigned int _id);
		void resetId();
		void resetId2D();
		unsigned int makeId(const char* _str);
		void setId(unsigned int id)
		{
			idStack.back() = id;
		}
		unsigned int getId() const
		{
			return idStack.back();
		}
		void pushId(unsigned int id)
		{
			idStack.push_back(id);
		}
		void popId()
		{
			assert(idStack.size() > 1);
			idStack.pop_back();
		}
		void pushEnableSorting(bool enable);
		void popEnableSorting();
		void setEnableSorting(bool enable);
		void setEnableLit(bool enable);
		bool getEnableSorting() const
		{
			return enableSortingStack.back();
		}

		unsigned int getLayerId() const
		{
			return layerIdStack.back();
		}
		void pushLayerId(unsigned int layer);
		void popLayerId();

		float pixelsToWorldSize(const Eigen::Vector3f& position, float pixels);
		Eigen::Vector3f screenToWorld(const Eigen::Vector2f& pos);

		void runDrawTask();
		void drawWidgets();
		void placeDrawTask(const std::string& name, std::function<void()> task);
		void cancleDrawTask(const std::string& name);
		void addGizmoWidget(EventWidget* widget);
		void removeGizmoWidget(EventWidget* widget);
		EventWidget* getGizmoWidget(const std::string&name);
		std::unordered_map<unsigned int, EventWidget*>& getGizmoWidgets() {
			return mGizmoWidgets;
		}
		template<class T>
		T* getGizmoWidgetAS(const std::string& name) {
			return dynamic_cast<T*>(getGizmoWidget(name));
		}
		bool isKeyDown(KeyAction key) const
		{
			return keyDownCurr[key];
		}
		bool wasKeyPressed(KeyAction key) const
		{
			return keyDownCurr[key] && !keyDownPrev[key];
		}
		bool wasKeyReleased(KeyAction key) const
		{
			return !keyDownCurr[key] && keyDownPrev[key];
		}

		void pushAlpha(float value);
		void popAlpha();
		void pushColor(const Eigen::Vector4<uint8_t>& color);
		void popColor();
		void pushSize(float value);
		void popSize();
		void pushMatrix(const Eigen::Matrix4f& mat);
		void popMatrix();
	private:
		Editor::Panels::SceneView* renderView = nullptr;
		/** Cell currently highlighted by the navigation cube (-1 = none). */
		int mViewCubeHoveredCell = -1;

	private:
		std::vector<std::string> cancelList;
		std::unordered_map<std::string, std::function<void()>> mDrawTaskMap;
		std::unordered_map<unsigned int, EventWidget*>mGizmoWidgets;
		
		Rendering::HAL::Texture mEmptyTexture2D;
		Rendering::HAL::Texture mEmptyTextureCube;
		Rendering::Data::Material* mLineMaterial = nullptr;
		Rendering::Data::Material* mPointMaterial = nullptr;
		Rendering::Data::Material* mTriangleMaterial = nullptr;
		Rendering::Data::Material* mCellMaterial = nullptr;
		Rendering::Data::Material* mLitMaterial = nullptr;
		std::vector<Eigen::Vector4<uint8_t>> colorStack;
		std::vector<float> alphaStack;
		std::vector<float> sizeStack;
		std::vector<unsigned int> idStack;
		std::vector<bool> enableSortingStack;
		std::vector<unsigned int> layerIdStack;

		std::vector<Eigen::Matrix4f> matrixStack;
		FrameParam cameraParam;
		bool keyDownCurr[KeyCount];
		bool keyDownPrev[KeyCount];

		unsigned int activeId;
		unsigned int hotId;
		unsigned int activeId2D;
		unsigned int hotId2D;
		float hotDepth;
		unsigned int appId;
		unsigned int appActiveId;
		unsigned int appHotId;
		Eigen::Vector3f gizmoStateVec3;
		Eigen::Vector3f gizmoStateStartAngle;
		Eigen::Matrix3f gizmoStateMat3;
		Eigen::Vector2f gizmoCursor;
		Eigen::Vector2f gizmoVec2;
		Eigen::Vector3f gizmoNormal;
		GizmoMode gizmoMode;
		bool gizmoLocal = false;
		float gizmoHeightPixels = 64.0f;
		float gizmoSizePixels = 5.0f;
		float gizmoStateFloat = 0.0f;

		// Primitive state.
		PrimitiveMode primMode;
		DrawPrimitiveType primType;
		unsigned int firstVertThisPrim;
		unsigned int vertCountThisPrim;
		Eigen::Vector3f minVertThisPrim;
		Eigen::Vector3f maxVertThisPrim;
		//unlit
		std::vector<VertexList*> vertexData[2];
		//lit
		VertexList litVertexArray;
		int vertexDataIndex;
		std::vector<unsigned int> layerIdMap;
		int layerIndex;
		std::vector<DrawList> drawLists;

		//Long-term lasting
		std::vector<PolygonInstance> drawLongTermMeshList;
		//short lasting
		std::vector<PolygonInstance> drawMeshList;
		std::unordered_map<std::string, PolygonMesh*>mPreStorePolygon;
		std::unordered_map<uint8_t,std::string >mPreStorePolygonId;
		int selectPolygonId = -1;
		int selectBlockId= -1;
		
		bool enableLit;
		bool sortCalled;
		bool endFrameCalled;
		int findLayerIndex(unsigned int id) const;
		VertexList* getCurrentVertexList();
		const DrawList* getDrawLists() const
		{
			return drawLists.data();
		}
		unsigned int getDrawListCount() const
		{
			return drawLists.size();
		}
	};
}
