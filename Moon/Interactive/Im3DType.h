#pragma once
#include <Eigen/Core>
#include <Maths/FMatrix4.h>
namespace Rendering {
	namespace Resources {
		class Texture;
		class Mesh;
		class Model;
	}
}

namespace MOON
{
	class ImRenderer;
	struct VertexFormat
	{
		Eigen::Vector4<float> positionSize; // xyz = position, w = size
		Eigen::Vector4<uint8_t> color;      // rgba8 (MSB = r)
		Eigen::Vector3<float> normal;
		Eigen::Vector2<float> uv;
		VertexFormat()
		{
		}
		VertexFormat(const Eigen::Vector3f& _position, float _size, Eigen::Vector4<uint8_t> _color)
			: positionSize(_position.x(), _position.y(), _position.z(), _size)
			, color(_color)
		{
		}
		VertexFormat(const Eigen::Vector3f& p, const Eigen::Vector4f& c, const Eigen::Vector3f& n, const Eigen::Vector2f& texCoord)
			: positionSize(p.x(), p.y(), p.z(), 1.0)
			, normal(n)
			, uv(texCoord)
			, color(c.w() * 255, c.z() * 255, c.y() * 255, c.x() * 255)
		{
		}
		VertexFormat(const Eigen::Vector3f& p, const Eigen::Vector4f& c, const Eigen::Vector3f& n)
			: positionSize(p.x(), p.y(), p.z(), 1.0)
			, normal(n)
			, uv(1.0, 1.0)
			, color(c.w() * 255, c.z() * 255, c.y() * 255, c.x() * 255)
		{
		}
		VertexFormat(const Eigen::Vector3f& p, const Eigen::Vector4<uint8_t>& c, const Eigen::Vector3f& n)
			: positionSize(p.x(), p.y(), p.z(), 1.0)
			, normal(n)
			, uv({ 1.0,1.0 })
			, color(c)
		{

		}
		VertexFormat(const Eigen::Vector3f& p, const Eigen::Vector4<uint8_t>& c, const Eigen::Vector3f& n, const Eigen::Vector2f& texCoord)
			: positionSize(p.x(), p.y(), p.z(), 1.0)
			, normal(n)
			, uv(texCoord)
			, color(c)
		{
		}
		VertexFormat(const Eigen::Vector3f& p, float blockId,const Eigen::Vector4<uint8_t>& c, const Eigen::Vector3f& n, const Eigen::Vector2f& texCoord)
			: positionSize(p.x(), p.y(), p.z(), blockId)
			, normal(n)
			, uv(texCoord)
			, color(c)
		{
		}
	};
	typedef std::vector<VertexFormat> VertexList;
	enum DrawPrimitiveType
	{
		// order here determines the order in which unsorted primitives are drawn
		DrawPrimitiveTriangles,
		DrawPrimitiveLines,
		DrawPrimitivePoints,
		DrawPrimitiveCount
	};

	struct DrawList
	{
		unsigned int layerId;
		DrawPrimitiveType primType;
		const VertexFormat* vertexData;
		unsigned int vertexCount;
	};

	enum KeyAction
	{
		MouseLeft,
		MouseMiddle,
		MouseRight,
		KeyL,
		KeyR,
		KeyS,
		KeyT,
		KeyControl,
		KeyCount,
		ActionSelect = MouseLeft,
		ActionControl = KeyControl,
		ActionGizmoLocal = KeyL,
		ActionGizmoRotation = KeyR,
		ActionGizmoScale = KeyS,
		ActionGizmoTranslation = KeyT,

		ActionCount
	};
	enum PrimitiveMode
	{
		PrimitiveModeNone,
		PrimitiveModePoints,
		PrimitiveModeLines,
		PrimitiveModeLineStrip,
		PrimitiveModeLineLoop,
		PrimitiveModeTriangles,
		PrimitiveModeTriangleStrip
	};
	enum GizmoMode
	{
		GizmoModeTranslation,
		GizmoModeRotation,
		GizmoModeScale
	};
	struct FrameParam
	{
		bool keyDown[KeyCount] = { false };
		float viewportWidth;
		float viewportHeight;
		float projectY = 1.0;
		bool orthProj = false;
		Eigen::Vector3f eye;
		Eigen::Vector3f viewDirectioin;
		Eigen::Vector3f rayOrigin;
		Eigen::Vector3f rayDirection;
		Eigen::Vector2<float> cursor;
		Eigen::Vector2<float> viewPortCursor;
		Eigen::Matrix4f view;
		Eigen::Matrix4f inverseView;
		Eigen::Matrix4f proj;
		Eigen::Matrix4f viewProj;
		float snapTranslation = 0.0f; // Snap value for translation gizmos (world units). 0 = disabled.
		float snapRotation = 0.0f;    // Snap value for rotation gizmos (radians). 0 = disabled.
		float snapScale = 0.0f;
		bool flipGizmoWhenBehind = true;
	};
	struct PolygonFace
	{
		std::vector<Eigen::Vector3f>vertex;
		std::vector<Eigen::Vector2f>uv;
		int blockId=0;
		Eigen::Vector3f n;
		Eigen::Vector4<uint8_t> color = { 255,255,255,255 };
		void clear();
		PolygonFace();

		PolygonFace(const Eigen::Vector3f& v0, const Eigen::Vector3f& v1, const Eigen::Vector3f& v2,const Eigen::Vector4<uint8_t>& c);
		void addPoint(const Eigen::Vector3f& v, const Eigen::Vector2f& tex);
		void addPointArray(const std::vector<Eigen::Vector3f>& v, const std::vector<Eigen::Vector2f>& tex);
		PolygonFace transform(const Eigen::Matrix4f& mat, float offsetX = 0.0f, float offsetY = 0.0f);
		void tranformUV(float u, float v);
	};
	struct PolygonInstance
	{
		PolygonInstance(const std::string& n, const Eigen::Matrix4f& m)
			: mesh(n)
			, model(m)
		{
			color = { 1.0f, 1.0f, 1.0f };
		}
		PolygonInstance(const std::string& n, const Eigen::Matrix4f& m, const Eigen::Vector3f& c)
			: mesh(n)
			, model(m)
			, color(c)
		{
		}
		PolygonInstance(std::string&& n, Eigen::Matrix4f&& m)
			: mesh(std::move(n))
			, model(std::move(m))
		{
			color = { 1.0f, 1.0f, 1.0f };
		}
		PolygonInstance(std::string&& n, Eigen::Matrix4f&& m, Eigen::Vector3f&& c)
			: mesh(std::move(n))
			, model(std::move(m))
			, color(c)
		{

		}
		void setFixed(bool flag) {
			fixScaled = flag;
		}
		float* getModelData()
		{
			return model.data();
		}
		std::string mesh;
		Eigen::Matrix4f model;
		Eigen::Vector3f color;
		bool fixScaled = false;
	};


	class PolygonMesh {
	public:
		void setCellColor(int index, const Eigen::Vector4<uint8_t>& color);
		int getBlockId(const std::string& name);
		void setBlockColor(int index,const Maths::FVector4& color);
		void addCell(const PolygonFace& cell);
		void switchNextBlock(const Maths::FVector4& color={1,1,1,1},const std::string& name="empty");
		void submit();
		void bind();
		Eigen::Vector3f getCellNormal(int index);
		PolygonMesh() = default;
		PolygonMesh(const std::vector<PolygonFace>& cells) :cellArray(cells) {
			vao = 0;
			vbo = 0;
			initGpuBuffer();
		}
		PolygonMesh(const std::vector<PolygonFace>&& cells) :cellArray(std::move(cells)) {
			vao = 0;
			vbo = 0;
			initGpuBuffer();
		}
		~PolygonMesh();
		void addMesh(Rendering::Resources::Mesh*mesh, const Maths::FMatrix4& matrix , const Eigen::Vector4<uint8_t>& c);
		void addModel(Rendering::Resources::Model*model,const Maths::FMatrix4& matrix,const Eigen::Vector4<uint8_t>& c);
		void initGpuBuffer();
		int hit(const Eigen::Matrix4f& viewProj, float u, float v);
		void setId(int id);
		int getId();


	public:
		Eigen::Matrix4f model = Eigen::Matrix4f::Identity();
		unsigned int numVertex = 0;
		Rendering::Resources::Texture* texture = nullptr;
		Rendering::Resources::Texture* edgeTexture = nullptr;
		Rendering::Resources::Texture* blockTexture = nullptr;
		std::vector<PolygonFace>cellArray;
		bool drawEdge = true;
		Eigen::Vector3f minConner = {FLT_MAX,FLT_MAX, FLT_MAX};
		Eigen::Vector3f maxConner = {FLT_MIN,FLT_MIN, FLT_MIN};
	private:
		void mergeBox(const Eigen::Vector3f& vertex);
		void mergeBox(const PolygonFace& face);
	protected:
		int id;
		std::vector<uint8_t>edgeValue;
		std::vector<Maths::FVector4>blockColor;
		std::unordered_map<std::string, int>blockNameToIndex;
		unsigned int vao = 0;
		unsigned int vbo = 0;
		unsigned int nextBlockId = 0;
		bool isDirty = false;
		bool blockColorDirty = true;
	};
	class GuiWidgetPolyMesh :public PolygonMesh {
	public:
		GuiWidgetPolyMesh() = default;
		GuiWidgetPolyMesh(const std::vector<PolygonFace>& cells) :PolygonMesh(cells) {
		}
		GuiWidgetPolyMesh(const std::vector<PolygonFace>&& cells) :PolygonMesh(cells) {

		}

		struct ScreenPositionInfo
		{
			int viewportSizeX = 1;
			int viewportSizeY = 1;
			int startX = 0;
			int startY = 0;
		};
		ScreenPositionInfo screenPos;
	protected:

	};
	GuiWidgetPolyMesh& NavigateCube();
	PolygonMesh& CoordAxis();
	PolygonMesh& TransformAxis();
	PolygonMesh& GizmoSketchPlane();

	/** Margin between the navigation cube and the viewport corner. */
	constexpr int kViewCubeMargin = 5;
	/** Side length of the square viewport the navigation cube renders into. */
	constexpr int kViewCubeSize = 125;

	/** Screen space placement of the navigation cube.
	 *
	 * Coordinates are scene-view pixels with the origin at the top-left corner,
	 * matching ScreenLayout and the mouse position; the GL viewport origin is
	 * derived from it. The rotate buttons of ViewCubeWidget anchor to the very
	 * same rectangle through ScreenLayout, so both agree by construction.
	 */
	struct ViewCubeLayout
	{
		/** Cube center in scene-view pixels. */
		float centerX = 0.0f;
		float centerY = 0.0f;
		/** Half extent of the square viewport the cube renders into. */
		float halfSize = kViewCubeSize * 0.5f;
		/** Bottom-left origin of the cube viewport, GL viewport coordinates. */
		int glViewportX = 0;
		int glViewportY = 0;
	};
	ViewCubeLayout ComputeViewCubeLayout(float p_viewportWidth, float p_viewportHeight);
}
