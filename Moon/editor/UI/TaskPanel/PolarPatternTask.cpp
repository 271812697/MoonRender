#include "editor/UI/TaskPanel/PolarPatternTask.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/component/TopoShapeActor.h"
#include "feature/SketcherFeature.h"
#include "feature/FeatureBody.h"
#include "TopoShape.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "Widgets/SliderFloatProperty.h"
#include "Widgets/EnumProperty.h"
namespace MOON {
    class PolarPatternTask::Internal {
    public:
        Internal(PolarPatternTask* pad , PolarPatternType type) :self(pad), polarType(type){
          
        }
        ~Internal() {
        }
  
    private:
        PolarPatternTask* self;
		PolarPatternType polarType;
        friend PolarPatternTask;
        bool isCreateFeature = false;
    };
    PolarPatternTask::PolarPatternTask(QWidget* parent, PolarPatternType type, Feature* feature)
        : ParamTaskDialog(parent), mInternal(new Internal(this,type)),ShapeHelper(feature)
    {

        setGenerateShapeName("Polar");
        PropertyComponent* p = addGroupParam("Polar");
        EnumProperty* polarMode = new EnumProperty("Polar Type", p);
        polarMode->setInitIndex(0);
        addParam(polarMode);
        EnumProperty* polarAxis = new EnumProperty("Axis", p); 
        polarAxis->setInitIndex(0);
        addParam(polarAxis);

        
        buildUi();
        previewShape();
    }
    QVariant PolarPatternTask::getParamValue(const QString& propertyName)
    {
        if (propertyName == "Polar:Polar Type") {
            QList<QString>list = { "Whole", "Feature" };
            return QVariant::fromValue(list);
        }
        else if (propertyName == "Polar:Axis") {
            QList<QString>list = { "Forward", "Reverse", "Double","Sysmetric"};
            return QVariant::fromValue(list);
        }
       
        return QVariant();
    }

    void PolarPatternTask::setParamValue(const QString& propertyName, const QVariant& value)
    {
        bool updatePreView = false;
        if (propertyName == "Polar:Polar Type") {
          
            updatePreView = true;
        }
        else if (propertyName == "Polar:Axis") {
         
            updatePreView = true;
        }
     
        if (updatePreView&& hasInitUi) {
            previewShape();
        }
    }
    PolarPatternTask::~PolarPatternTask()
    {
        delete mInternal;
    }
    void PolarPatternTask::clickOk()
    {
        generateFinalShape();
    }
    void PolarPatternTask::clickApply()
    {
    }
    void PolarPatternTask::clickCancel()
    {
        clearPreviewShape();
        if (mInternal->isCreateFeature) {
           // mInternal->feature->RemoveFromScene();
            //delete mInternal->feature;
        }
    }
}
