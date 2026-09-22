#include <tracy/Tracy.hpp>
#include "editor/UI/TaskPanel/RevolutionTask.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/EnumProperty.h"
#include "Widgets/BoolProperty.h"
#include "TopoShape.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/ViewTool.h"
#include "feature/SketcherFeature.h"
#include "feature/RevolveFeature.h"
#include "feature/FeatureBody.h"
#include "Interactive/Widgets/ArrowRotateWidget.h"

#include <gp_Vec.hxx>
#include <gp_Dir.hxx>

#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
namespace MOON {
    class RevolutionTask::Internal {
    public:
        Internal(RevolutionTask*s, RevolutionType type)
            :self(s),mType(type)
        {
            auto f=self->getFeature();
            if (f) {
                feature = dynamic_cast<RevolveFeature*>(f);
                //faceShape = feature->getProfileFace();
            }
            else
            {  
                Part::TopoShape faceShape;
                isCreatedFeature = true;
                feature = new RevolveFeature("Revolve",mType== RevolutionType::ReAdditive?0:1);
                self->setFeature(feature);
                Feature* baseFeature = nullptr;
                std::vector<std::string>subValues;
                ViewTool::getSelectedBasedFeature(baseFeature, subValues);
                if (baseFeature) {
                    feature->setBaseFeature(baseFeature);
                    feature->setSubValues(subValues);
                    faceShape = feature->getProfileFace();
                }
                else
                {
                    // 1. 获取当前激活的草图,
                    auto* sketchFeature = SketcherObjManager::instance().GetLastSketcherFeature();
                    if (sketchFeature) {
                        //2.设置基于最后一个feature
                        FeatureBody::instance().setBaseFeatureFor(feature);
                        feature->setProfile(sketchFeature);
                        // Through the feature, so the pose of the sketch is taken into
                        // account here exactly like it is where the solid is built.
                        faceShape = feature->getProfileFace();
                    }
                }
                Part::TopoShape tempShape = faceShape.makeElementFace(nullptr, "Part::FaceMakerCheese");
                GProp_GProps props;
                BRepGProp::SurfaceProperties(tempShape.getSubTopoShape(TopAbs_FACE, 1).getShape(), props);
                gp_Pnt cog = props.CentreOfMass();
                feature->origin[0] = cog.X();
                feature->origin[1] = cog.Y();
                feature->origin[2] = cog.Z();
            }
            if (feature) {  
                mBehaviour = new ArrowRotateWidget("rotate");
                mBehaviour->setUpOriginPos(feature->origin[0], feature->origin[1], feature->origin[2]);
                mBehaviour->setAngle(feature->angle);
                mBehaviour->setImmediateInvoke(false);
                mBehaviour->AddObserver(ArrowRotateEvent::AngleChange, self, &RevolutionTask::onAngleChange);
            }
        }
        void setAxis(const gp_Ax1& ax) {
            feature->axis = ax;
            gp_Ax1 tempAxis = feature->axis;
            if (feature->reverse) {
                tempAxis.Reverse();
            }
            mBehaviour->setUpRotateAxis(
                tempAxis.Direction().X(), tempAxis.Direction().Y(), tempAxis.Direction().Z());
            mBehaviour->setUpRotateCenter(ax.Location().X(),
                tempAxis.Location().Y(), tempAxis.Location().Z());
            mBehaviour->setAngle(feature->angle);

        }
        bool setSketcherAxis() {
            // 1. 获取当前激活的草图
            SketcherObj* sketchObj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
            if (sketchObj) {
                Base::Vector3d origin = sketchObj->getPlaneOrigin();
                Base::Vector3d saxis = sketchObj->getPlaneXAxis();
                if (feature->axisType == 1) {
                    saxis = sketchObj->getPlaneYAxis();
                }
                gp_Pnt b(origin.x, origin.y, origin.z);;
                gp_Dir d(saxis.x, saxis.y, saxis.z); 
                setAxis(gp_Ax1(b, d));
                return true;
            }        
            return false;
        }
        void onSelectAny() {
        }
        ~Internal() {
            if (mBehaviour) {
                delete mBehaviour;
            }
        }
    private:
        ArrowRotateWidget* mBehaviour = nullptr;
        SliderFloatProperty* angleProp;
        friend RevolutionTask;
        RevolutionTask* self = nullptr;
        RevolutionType mType;
       
        RevolveFeature* feature=nullptr;
        bool isCreatedFeature = false;

    };

    RevolutionTask::RevolutionTask(RevolutionType type,QWidget* parent, Feature* feature )
        : ParamTaskDialog(parent),ShapeHelper(feature), mInternal(new Internal(this,type))
    {       
        mInternal->mType = type;
        setGenerateShapeName("RevolutionShape");

        mPreviewOption.isTransparent = true;
        mPreviewOption.isBlend = true;
        mPreviewOption.useDomainColor = false;
        mPreviewOption.g = 0;
        mPreviewOption.b = 0;
        PropertyComponent* p=addGroupParam("Revolve");
        mInternal->angleProp = new SliderFloatProperty("Angle", p);
        mInternal->angleProp->setMinMax(-360,360);

        addParam(mInternal->angleProp);
        EnumProperty* join = new EnumProperty("Axis", p);
        addParam(join);
        BoolProperty* reverse = new BoolProperty("Reverse", p);
        addParam(reverse);
        buildUi();
        initilized = true;
        previewShape();
    }

    RevolutionTask::~RevolutionTask()
    {
        delete mInternal;
    }

    QVariant RevolutionTask::getParamValue(const QString& propertyName)
    {
        if (propertyName == "Revolve:Angle") {
            return QVariant::fromValue(mInternal->feature->angle);
        }
        else if (propertyName == "Revolve:Axis") {
            QList<QString>list = {"Sketch X", "Sketch Y" ,"Select Edge"  };
            return QVariant::fromValue(list);
        }
       
        else if (propertyName == "Revolve:Reverse") {
            return QVariant::fromValue(mInternal->feature->reverse);
        }
        return QVariant();
    }

    void RevolutionTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        ZoneScoped;
        bool updatePreView = false;
        if (propertyName == "Revolve:Angle") {
            mInternal->feature->angle = value.toFloat();
            mInternal->mBehaviour->setAngle(mInternal->feature->angle);
            updatePreView = true;
        }
        else if (propertyName == "Revolve:Axis") {
            mInternal->feature->axisType = value.value<int>();
            if (mInternal->feature->axisType != 2) {
                if (mInternal->setSketcherAxis()) {
                    updatePreView = true;
                }
            }
            else
            {
                mInternal->setAxis(mInternal->feature->axis);
                updatePreView = true;
            }
        }
        else if (propertyName == "Revolve:Reverse") {
            mInternal->feature->reverse = value.toBool();
			mInternal->setAxis(mInternal->feature->axis);
            updatePreView = true;
        }
        if (updatePreView&&initilized) {
            previewShape();
        }
    }


    void RevolutionTask::clickOk()
    {
        generateFinalShape();
    }
    void RevolutionTask::clickApply()
    {
    }
    void RevolutionTask::clickCancel()
    {
        clearPreviewShape();
        if (mInternal->isCreatedFeature) {
            mInternal->feature->RemoveFromScene();
            delete mInternal->feature;
        }
    }
    void RevolutionTask::onSelectEdge(const std::vector<Part::TopoShape>& edge)
    {  
        ZoneScoped;
        if (mInternal->feature->axisType == 2) {
            TopoDS_Edge refEdge = TopoDS::Edge(edge[1].getShape());
            BRepAdaptor_Curve adapt(refEdge);
            gp_Pnt b;
            gp_Dir d;
            GeomAbs_CurveType curveType = adapt.GetType();
            if (curveType == GeomAbs_Line) {
                b = adapt.Line().Location();
                d = adapt.Line().Direction();
            }
            else if (curveType == GeomAbs_Circle) {
                b = adapt.Circle().Location();
                d = adapt.Circle().Axis().Direction();
            }
            else
            {
                double middle = adapt.FirstParameter() + adapt.LastParameter();
                gp_Vec v;
                adapt.D1(middle, b, v);
                d = v;
            }
            mInternal->setAxis(gp_Ax1(b, d));
            previewShape();
        }
    }
    void RevolutionTask::onAngleChange()
    {
        ZoneScoped;
        mInternal->feature->angle= mInternal->mBehaviour->getAngle();
        mInternal->angleProp->updateWidgetValue(mInternal->feature->angle);
    }
}
