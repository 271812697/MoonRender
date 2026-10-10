#pragma once
#include <vector>
#include <Core/ECS/Components/AComponent.h>
namespace Part {
	class TopoShape;
}
namespace Core::ECS { class Actor; }
namespace Core::ECS::Components
{
	struct HighLightOption
	{
		enum Mode
		{
			Color=0,
			Transparent=1
		};
		Mode mode{ Color };
		Maths::FVector4 hoverColor = { 1,0.706,0,1 };
		Maths::FVector4 selectColor = {1,0.510,0,1};
	};
	class CTopoShape : public AComponent
	{
	public:
		CTopoShape(ECS::Actor& p_owner);
		virtual ~CTopoShape()override;
		std::string GetName() override;
		HighLightOption& getHightLightOption();
		void switchHighLightMode(HighLightOption::Mode mode);
		/** The colour the faces of this shape are drawn with.
		 *
		 * A shape's colour is part of the shape, not of the renderer: it is what a
		 * feature carries around and what a document writes into the file. It is put
		 * on the u_Albedo uniform of the face material of the render anchor below the
		 * actor ("AllFaces") - the very uniform the material panel of that actor
		 * edits, and the very material TopoActor() makes for the actor. There is one
		 * material, so nothing here makes a second one.
		 *
		 * Reading gives back what the material holds when there is a material, so a
		 * colour edited in the material panel is seen here too. */
		Maths::FVector4 GetColor() const;
		/** Sets that colour. The material is updated right away unless the render
		 * anchors are not there yet (see ApplyColor). */
		void SetColor(const Maths::FVector4& p_color);
		/** Puts the colour back onto the material. The component is made before the
		 * render anchors of its actor are, so a colour that arrives earlier has
		 * nowhere to go until this runs - TopoActor() calls it once the material
		 * exists. */
		void ApplyColor();
		virtual void OnUpdate(float p_deltaTime) override;
		void updateChildBuffer();
		std::vector<std::pair<int, int>>GetChildMeshInfo();
		void setChildsMeshTransParent(const std::vector<int>& childs,bool updateBuffer=true);
		Part::TopoShape& GetTopoShape();
		Part::TopoShape GetTopoFace(int childFaceId);
		Part::TopoShape GetTopoEdge(int childFaceId);
		Part::TopoShape GetTopoVertex(int childVertexId);
		void hoverChild(int childId);
		void selectChildFaces(const std::vector<int>&childIds);
		void hoverChildLine(int childId);
		void selectChildLines(const std::vector<int>& childIds);
		void hoverChildVertex(int childId);
		void selectChildVertex(const std::vector<int>& childIds);
		void clearHover();
		void clearHoverLine();
		void clearSelectLines();
		void clearHoverVertex();
		void clearSelectVertex();
		void discretizationFaceShape();
		void discretizationEdgeShape();
		void discretizationVertexShape();
		void discretizationShape();
		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)override;
	private:
		void updateChildMesh();
		void updateEdgeMesh();
		void updateVertexMesh();
		void rebuildTopologyTree();
		Core::ECS::Actor* getOrCreateTopoGroup(int shellIndex, const std::string& fallbackName);
		class CTopoShapeInternal;
		CTopoShapeInternal* mInternal = nullptr;
	};
}
