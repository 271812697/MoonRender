#pragma once
#include "Widgets/Property.h"
#include "Widgets/FVec2.h"
namespace MOON {
    // The editor of a float2 material parameter (u_TextureTiling, u_TextureOffset, ...).
    class Vec2Property : public WidgetProperty {
    public:
        Vec2Property(const QString& n, PropertyComponent* comp);
        ~Vec2Property();
        virtual PropertyQtWidget* createEditorWidget(QWidget* parent = nullptr) override;
    };
}
