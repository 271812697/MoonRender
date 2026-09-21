#pragma once
#include <vector>
#include <string>
namespace Core::ECS
{
    class Actor;
}
namespace Part {
    class TopoShape;
}
namespace MOON
{
    class Feature;
    class ViewTool {
    public:
        static Core::ECS::Actor* getLastestActorSelected();
        static bool getSelectedTopoShape(std::vector<Part::TopoShape>&topo);
        static Feature* getSelectedFeature();
        static bool getSelectedBasedFeature(Feature*& f, std::vector<std::string>& subValues);
        /** Resolves one actor of the scene to the feature that owns it plus the
         * reference of the sub-shape it stands for ("Edge_3", "Face_1", ...).
         * The topology leaves (the Face_ and Edge_ actors) live at an arbitrary
         * depth below the feature, so the whole parent chain is walked. */
        static bool getActorBasedFeature(
            Core::ECS::Actor* actor,
            Feature*& f,
            std::string& subValue);
        static Core::ECS::Actor* createTopoActor(const Part::TopoShape& topo,const char* name=nullptr);
    };
}
