#pragma once
#include "Interactive/Widgets/DrawSketchHandler.h"
#include "Interactive/Widgets/OnViewParameters.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "renderer/SceneView.h"
#include "core/component/CGeometryLine.h"
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include "Geometry.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"

#include <Core/ECS/Actor.h>
#include <type_traits>
#include <optional>
#include <Eigen/Core>
namespace MOON
{
    template<typename T>
    auto toPointerVector(const std::vector<std::unique_ptr<T>>& vector)
    {
        std::vector<T*> vp(vector.size());

        std::transform(vector.begin(), vector.end(), vp.begin(), [](auto& p) { return p.get(); });

        return vp;
    }
    using Vec2 = Eigen::Vector2f;
    namespace StateMachines
    {

        enum class OneSeekEnd
        {
            SeekFirst,
            End  // MUST be the last one
        };

        enum class TwoSeekEnd
        {
            SeekFirst,
            SeekSecond,
            End  // MUST be the last one
        };

        enum class ThreeSeekEnd
        {
            SeekFirst,
            SeekSecond,
            SeekThird,
            End  // MUST be the last one
        };

        enum class FourSeekEnd
        {
            SeekFirst,
            SeekSecond,
            SeekThird,
            SeekFourth,
            End  // MUST be the last one
        };

        enum class FiveSeekEnd
        {
            SeekFirst,
            SeekSecond,
            SeekThird,
            SeekFourth,
            SeekFifth,
            End  // MUST be the last one
        };

        enum class TwoSeekDoEnd
        {
            SeekFirst,
            SeekSecond,
            Do,
            End  // MUST be the last one
        };

    }  // namespace StateMachines
    template<typename SelectModeT>
    class StateMachine
    {
    public:
        StateMachine()
            : Mode(static_cast<SelectModeT>(0))
        {
        }
        virtual ~StateMachine()
        {
        }

    protected:
        void setState(SelectModeT mode)
        {
            Mode = mode;
            onModeChanged();
        }

        void ensureState(SelectModeT mode)
        {
            if (Mode != mode) {
                Mode = mode;
                onModeChanged();
            }
        }

        /** Ensure the state machine is the provided mode
         * but only if the mode is an earlier state.
         *
         * This allows one to return to previous states (e.g.
         * for modification), only if that state has previously
         * been completed.
         */
        void ensureStateIfEarlier(SelectModeT mode)
        {
            if (Mode != mode) {
                if (mode < Mode) {
                    Mode = mode;
                    onModeChanged();
                }
            }
        }

        SelectModeT state() const
        {
            return Mode;
        }

        bool isState(SelectModeT state) const
        {
            return Mode == state;
        }

        void setNextState(std::optional<SelectModeT> nextState)
        {
            nextMode = nextState;
        }

        std::optional<SelectModeT> getNextState()
        {
            return nextMode;
        }

        void applyNextState()
        {
            if (nextMode) {
                auto next = std::move(*nextMode);
                nextMode = std::nullopt;
                setState(next);
            }
        }

        bool isFirstState() const
        {
            return Mode == (static_cast<SelectModeT>(0));
        }

        bool isLastState() const
        {
            return Mode == SelectModeT::End;
        }

        constexpr SelectModeT getFirstState() const
        {
            return static_cast<SelectModeT>(0);
        }

        SelectModeT computeNextMode() const
        {
            auto modeint = static_cast<int>(state());

            if (modeint < maxMode) {
                auto newmode = static_cast<SelectModeT>(modeint + 1);
                return newmode;
            }
            else {
                return SelectModeT::End;
            }
        }

        void moveToNextMode()
        {
            setState(computeNextMode());
        }

        void reset()
        {
            
            nextMode = std::nullopt;
            if (Mode != static_cast<SelectModeT>(0)) {
                setState(static_cast<SelectModeT>(0));
            }
        }

        virtual bool onModeChanged()
        {
            return true;
        };

    private:
        SelectModeT Mode;
        std::optional<SelectModeT> nextMode;
        static const constexpr int maxMode = static_cast<int>(SelectModeT::End);
    };
    namespace ConstructionMethods
    {

        enum class DefaultConstructionMethod
        {
            End  // Must be the last one
        };

    }  // namespace ConstructionMethods

    template<typename ConstructionMethodT>
    class ConstructionMethodMachine
    {
    public:
        ConstructionMethodMachine(
            ConstructionMethodT constructionmethod = static_cast<ConstructionMethodT>(0)
        )
            : ConstructionMode(constructionmethod)
        {
        }
        virtual ~ConstructionMethodMachine()
        {
        }

    protected:
        void setConstructionMethod(ConstructionMethodT mode)
        {
            ConstructionMode = mode;
            onConstructionMethodChanged();
        }

        ConstructionMethodT constructionMethod() const
        {
            return ConstructionMode;
        }

        bool isConstructionMethod(ConstructionMethodT state) const
        {
            return ConstructionMode == state;
        }

        void resetConstructionMode()
        {
            ConstructionMode = static_cast<ConstructionMethodT>(0);
        }

        void initConstructionMethod(ConstructionMethodT mode)
        {
            ConstructionMode = mode;
        }

        // Cyclically iterate construction methods
        ConstructionMethodT getNextMethod() const
        {
            auto modeint = static_cast<int>(ConstructionMode);


            if (modeint < (maxMode - 1)) {
                auto newmode = static_cast<ConstructionMethodT>(modeint + 1);
                return newmode;
            }
            else {
                return static_cast<ConstructionMethodT>(0);
            }
        }

        void iterateToNextConstructionMethod()
        {
            if (ConstructionMethodsCount() > 1) {
                setConstructionMethod(getNextMethod());
            }
        }

        virtual void onConstructionMethodChanged() {};

        static constexpr int ConstructionMethodsCount()
        {
            return maxMode;
        }

    private:
        ConstructionMethodT ConstructionMode;
        static const constexpr int maxMode = static_cast<int>(ConstructionMethodT::End);
    };
    template<
        typename HandlerT,            // A type for which the handler template is instantiated
        typename SelectModeT,         // The state machine defining the states that the handle iterates
        int PInitAutoConstraintSize,  // The initial size of the AutoConstraint>
        typename ConstructionMethodT = ConstructionMethods::DefaultConstructionMethod>
    class DrawSketchDefaultHandler : public DrawSketchHandler,
        public StateMachine<SelectModeT>,
        public ConstructionMethodMachine<ConstructionMethodT> {
    public:
        using DSDH = DrawSketchDefaultHandler<HandlerT, SelectModeT, PInitAutoConstraintSize, ConstructionMethodT>;
        DrawSketchDefaultHandler(const std::string&name,
            ConstructionMethodT constructionmethod = static_cast<ConstructionMethodT>(0)
        )
            : DrawSketchHandler(name),
            StateMachine<SelectModeT>(),
            ConstructionMethodMachine<ConstructionMethodT>(constructionmethod),
            continuousMode(true)
        {            

            setActive(false);
            makePlane(SketcherPlane2D());
        }
        ~DrawSketchDefaultHandler() override
        {
        }

        virtual void onReset()
        {
        }
        void reset()
        { 
            clearEdit();
            //for (auto& ac : sugConstraints) {
            //    ac.clear();
            //}

            //AutoConstraints.clear();
            ShapeGeometry.clear();
            //ShapeConstraints.clear();

            // The numbers the tool was given were for the run that just ended: the next
            // one is driven by the mouse until the user types in a box again.
            for (int i = 0; i < onViewParameters.size(); i++) {
                onViewParameters.at(i)->release();
            }

            onReset();

            ModeStateMachine::reset();

            //applyCursor();
        }
        bool handleContinuousMode()
        {

            if (continuousMode) {
                // This code enables the continuous creation mode.
                reset();
                // It is ok not to call to purgeHandler in continuous creation mode because the
                // handler is destroyed by the quit() method on pressing the right button of the mouse
                return false;
            }
            else {
                //sketchgui->purgeHandler();  // no code after, Handler get deleted in ViewProvider
                return true;
            }
        }
        virtual void executeCommands() {
            CORE_INFO("add {0} geometry to sketcher obj ", ShapeGeometry.size());

            SketcherObj* sketchobj=SketcherObjManager::instance().GetCurrentActiveSketcherObj();
            if (!sketchobj) {
                return;
            }
            // Elements explicitly created as construction geometry (the flag
            // lives on the Part::Geometry object) stay construction in the
            // sketch object.
            for (auto& geo : ShapeGeometry) {
                if (!geo) {
                    continue;
                }
                // Read the flag before addGeometry(): the unique_ptr overload
                // moves the element into the sketch object and leaves it empty.
                const bool isConstruction = geo->getConstruction();
                const int geoId = sketchobj->addGeometry((geo));
                if (isConstruction) {
                    sketchobj->setConstruction(geoId, true);
                }
            }
        }
        bool finish()
        {
            if (this->isState(SelectMode::End)) {
                //unsetCursor();
                //resetPositionText();

                //try {
                    executeCommands();

                //    if (sugConstraints.size() > 0) {
                //        beforeCreateAutoConstraints();

                //        generateAutoConstraints();

                //        createAutoConstraints();
                //    }
                //}
                //catch (const Base::RuntimeError& e) {
                //    // RuntimeError exceptions inside of the block above must provide a translatable
                //    // message. It is reported both to developer (report view) and user (notifications
                //    // area).
                //    Base::Console().error(e.what());
                //}

                //// Keep the recompute separate so that everything is drawn even if execution fails
                //// partially
                //try {
                //    tryAutoRecomputeIfNotSolve(sketchgui->getSketchObject());
                //}
                //catch (const Base::RuntimeError& e) {
                //    // RuntimeError exceptions inside of the block above must provide a translatable
                //    // message. It is reported both to developer (report view) and user (notifications
                //    // area).
                //    Base::Console().error(e.what());
                //}
                //return handleContinuousMode();
                return handleContinuousMode();
            }
            return false;
        }
        virtual void updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
        {

        }
        virtual void rightButtonOrEsc() {
            if (this->isFirstState()) {
                quit();
            }
            else
            {
                handleContinuousMode();
            }
        }
        virtual void onLeftMousePressed()override {
            ButtonPressParse();
        }
        virtual void onLeftMouseReleased()override {
            ButtonReleaseParse();
        }
        virtual void onMouseMove()override {
            DrawSketchHandler::onMouseMove();
            MouseMoveParse();
        }
        virtual void onRightMousePressed()override {
            rightButtonOrEsc();
        }

        void ButtonPressParse() {
            onButtonPressed(onSketchPos);
        }
        void ButtonReleaseParse() {
            releaseButton(onSketchPos);
        }
        void MouseMoveParse() {
            mouseMove(onSketchPos);
        }
        virtual void mouseMove(Base::Vector2d pos)
        {
            // The order is FreeCAD's: the values typed in the boxes get their say first,
            // then the geometry is built from the position that comes out of that, and
            // finally the live values go back into the boxes.
            enforceControlParameters(pos);
            updateDataAndDrawToPosition(pos);
            adaptParameters(pos);
        }
        virtual void onButtonPressed(Base::Vector2d pos)
        {
            enforceControlParameters(pos);
            this->updateDataAndDrawToPosition(pos);
            adaptParameters(pos);
            if (canGoToNextMode()) {
                this->moveToNextMode();
            }
        }
        virtual void onKeyPress(const std::string& key) {
            if (isEditingOnViewParameter()) {
                // The keyboard is inside one of the boxes: the tool must not take the
                // keys for its shortcuts (typing "m" into a box is not a shortcut). A
                // handler that overrides onKeyPress without calling this one has to
                // check isEditingOnViewParameter() itself.
                return;
            }
            if (key == "TAB") {
                // Tab moves on to the next box that is still waiting for a number (the
                // keyboard has to go back to the viewport for this to be seen, which is
                // what clicking in it does).
                onViewParameters.focusNextUnfinished();
                return;
            }
            if (key=="M" && !this->isLastState()) {
                this->iterateToNextConstructionMethod();
            }
        }

        /** @name on-view parameters -------------------------------------------------
         *
         * The boxes drawn over the viewport that a tool can be given numbers in, instead
         * of positions with the mouse (FreeCAD's on-view parameters). A tool adds the
         * ones it wants while it is activated, says which step of the machine each of
         * them belongs to, and calls drawOnViewParameters() from its per-frame update.
         */
        //@{

        /** The values typed in take the mouse position over: a parameter that has been
         * typed in wins over where the cursor is. This is where a tool maps its
         * parameters onto the position it is about to build geometry from. */
        virtual void enforceControlParameters(Base::Vector2d& /*p_onSketchPos*/)
        {
        }

        /** The other way round: what the mouse decided is written into the boxes of the
         * step the machine is in, so that they show the live value (and the tool puts
         * each box where the geometry it belongs to is). */
        virtual void adaptParameters(Base::Vector2d /*p_onSketchPos*/)
        {
        }

        /** Which step of the machine a parameter is the input of. Only the parameters of
         * the current step are shown, and only those count as "what this step asks
         * for" when the tool decides whether it can go on. */
        virtual SelectModeT stateOfOnViewParameter(int /*p_index*/) const
        {
            return this->getFirstState();
        }

        /** True when the step the machine is in is the last one that asks for numbers,
         * even if the machine has steps after it. A tool that is finished before its last
         * seek state overrides this (the circle is: a centre and a radius is a circle). */
        virtual bool isLastControlStep() const
        {
            return false;
        }

        /** Draws the boxes of the current step. Call it from the tool's per-frame
         * update - that is inside the ImGui frame, where the boxes are drawn. */
        void drawOnViewParameters()
        {
            if (onViewParameters.size() == 0) {
                // A tool that has no boxes up (it is not running, or it has just been
                // activated and not configured itself yet): the next set it puts up is
                // one the user is shown for the first time.
                m_onViewParametersShown = false;
                m_lastOnViewState = -1;
                return;
            }

            for (int i = 0; i < onViewParameters.size(); i++) {
                OnViewParameter* parameter = onViewParameters.at(i);
                parameter->setVisible(stateOfOnViewParameter(i) == this->state());
            }

            if (!m_onViewParametersShown || m_lastOnViewState != static_cast<int>(this->state())) {
                // A step that has just been entered is the one the user is being asked
                // about: its first box that is still unanswered takes the keyboard by
                // itself, so that a number can be typed straight away without pointing at
                // the box (this is what FreeCAD's controller does when it activates the
                // parameters of a mode: the spin box comes up focused, with the number in
                // it selected).
                onViewParameters.focusFirstUnfinished();
                m_onViewParametersShown = true;
                m_lastOnViewState = static_cast<int>(this->state());
            }

            onViewParameters.update();
        }

        /** True while the keyboard is inside one of the boxes. */
        bool isEditingOnViewParameter() const
        {
            return onViewParameters.isEditing();
        }

        /** Goes on to the next step when every box of the current one has been confirmed:
         * this is what lets a tool that was given all its numbers place the geometry by
         * itself, without a click. On the last step that means the geometry is committed
         * here and the tool starts over (or ends, when it is not continuous). */
        /** @return true when the machine really moved on. */
        bool advanceOnViewParametersIfComplete()
        {
            bool anythingTyped = false;
            for (int i = 0; i < onViewParameters.size(); i++) {
                const OnViewParameter* parameter = onViewParameters.at(i);
                if (stateOfOnViewParameter(i) != this->state() || !parameter->isVisible()) {
                    continue;
                }
                if (!parameter->hasFinishedEditing()) {
                    return false;  // this step is still waiting for one of its numbers
                }
                anythingTyped = true;
            }
            if (!anythingTyped || this->state() == SelectMode::End) {
                return false;
            }
            if (this->isLastControlStep()) {
                this->setState(SelectMode::End);
                finish();
                return true;
            }
            this->moveToNextMode();
            if (this->state() == SelectMode::End) {
                // Nothing left to ask: the geometry is what the numbers say, and the tool
                // is not waiting for a click to place it.
                finish();
            }
            return true;
        }

        /** A box was confirmed. The tool's own reaction (rebuilding the geometry) has
         * happened already; this is the machine's part of it: the step goes on when it has
         * all the numbers it asks for, and until then the keyboard moves to the next box
         * that is still waiting, so that the numbers of one step can be typed one after the
         * other without pointing at each box. */
        void finishOnViewParameterStep(int p_index, double p_value)
        {
            if (!advanceOnViewParametersIfComplete()) {
                onViewParameters.focusNextUnfinished();
            }
        }
        //@}
        virtual bool releaseButton(Base::Vector2d onSketchPos)
        {
            if (finish()) {

            }
            return true;
        }
        virtual bool canGoToNextMode()
        {
            return true;
        }
        virtual void createShape(bool onlyeditoutline)
        {
            
        }
        void CreateAndDrawShapeGeometry()
        {
            clearEdit();
            createShape(true);
            drawEdit(toPointerVector(ShapeGeometry));
        }

        /** @brief Function to add a line to the ShapeGeometry vector.*/
        auto addLineToShapeGeometry(Base::Vector3d p1, Base::Vector3d p2, bool constructionMode)
        {
            auto line = std::make_unique<Part::GeomLineSegment>();
            line->setPoints(p1, p2);
            line->setConstruction(constructionMode);
            return static_cast<Part::GeomLineSegment*>(ShapeGeometry.emplace_back(std::move(line)).get());
        }
        /** @brief Function to add an arc to the ShapeGeometry vector.*/
        auto addArcToShapeGeometry(Base::Vector3d p1, double start, double end, double radius, bool constructionMode)
        {
            auto arc = std::make_unique<Part::GeomArcOfCircle>();
            arc->setCenter(p1);
            arc->setRange(start, end, true);
            arc->setRadius(radius);
            arc->setConstruction(constructionMode);
            return static_cast<Part::GeomArcOfCircle*>(ShapeGeometry.emplace_back(std::move(arc)).get());
        }

        /** @brief Function to add a point to the ShapeGeometry vector.*/
        auto addPointToShapeGeometry(Base::Vector3d p1, bool constructionMode)
        {
            auto point = std::make_unique<Part::GeomPoint>();
            point->setPoint(p1);
            point->setConstruction(constructionMode);
            return static_cast<Part::GeomPoint*>(ShapeGeometry.emplace_back(std::move(point)).get());
        }

        /** @brief Function to add an ellipse to the ShapeGeometry vector.*/
        auto addEllipseToShapeGeometry(
            Base::Vector3d centerPoint,
            Base::Vector3d majorAxisDirection,
            double majorRadius,
            double minorRadius,
            bool constructionMode
        )
        {
            auto ellipse = std::make_unique<Part::GeomEllipse>();
            ellipse->setMajorRadius(majorRadius);
            ellipse->setMinorRadius(minorRadius);
            ellipse->setMajorAxisDir(majorAxisDirection);
            ellipse->setCenter(centerPoint);
            ellipse->setConstruction(constructionMode);
            return static_cast<Part::GeomEllipse*>(ShapeGeometry.emplace_back(std::move(ellipse)).get());
        }

        /** @brief Function to add a circle to the ShapeGeometry vector.*/
        auto addCircleToShapeGeometry(Base::Vector3d centerPoint, double radius, bool constructionMode)
        {
            auto circle = std::make_unique<Part::GeomCircle>();
            circle->setRadius(radius);
            circle->setCenter(centerPoint);
            circle->setConstruction(constructionMode);
            return static_cast<Part::GeomCircle*>(ShapeGeometry.emplace_back(std::move(circle)).get());
        }
    protected:
        using SelectMode = SelectModeT;
        using ModeStateMachine = StateMachine<SelectModeT>;
        using ConstructionMethod = ConstructionMethodT;
        using ConstructionMachine = ConstructionMethodMachine<ConstructionMethodT>;

        std::vector<std::unique_ptr<Part::Geometry>> ShapeGeometry;
        /** The boxes the tool can be given numbers in (see above). */
        OnViewParameters onViewParameters;
        /** The step the boxes were last drawn for, and whether they have been drawn at
         * all: entering a step is what puts the keyboard in its first box. */
        int m_lastOnViewState = -1;
        bool m_onViewParametersShown = false;
        bool continuousMode;
    };
}
