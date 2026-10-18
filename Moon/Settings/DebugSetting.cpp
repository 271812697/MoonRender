#include "DebugSetting.h"
#include "dataDispatcher.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "core/callbackManager.h"
#include "Widgets/checkbox.h"
#include <QSlider>
namespace MOON {
	static std::unordered_map<size_t,std::string >mHashCode;
	class NodeBoolWidget:public SlidingCheckBox {
	public:
		NodeBoolWidget(NodeBase* node ,QWidget* parent = nullptr, const QString& text = "") :SlidingCheckBox(parent,text),mNode(node){
			connect(this, &SlidingCheckBox::clicked, [this](bool value) {
			
				mNode->setData<bool>(this->checkValue());
			});
		}
	private:
		NodeBase* mNode = nullptr;
	};
	class DragFloat :public NodeData<float> {
	public:
		DragFloat(float v,float a,float b,const char* n) :NodeData(v,n),mMinValue(a),mMaxValue(b){
		}
		virtual QWidget* createWidget(QWidget* parent)override
		{
			QSlider* slider = new QSlider(Qt::Horizontal, parent);
			slider->setRange(0, 100); // 鏁存暟鑼冨洿
			if (mMaxValue > mMinValue) {
				slider->setValue(static_cast<int>(
					(this->get() - mMinValue) / (mMaxValue - mMinValue) * 100.0f
				));
			}
			QObject::connect(slider, &QSlider::valueChanged, [this](int v) {
				float val=v / 100.0f * (mMaxValue - mMinValue) + mMinValue;
				this->setData<float>(val);
				});
			return slider;
		}
	private:
		float mMinValue;
		float mMaxValue;
	};
	class DragInt :public NodeData<int> {
	public:
		DragInt(int v, int a, int b, const char* n) :NodeData(v, n), mMinValue(a), mMaxValue(b) {
		}
		virtual QWidget* createWidget(QWidget* parent)override
		{
			QSlider* slider = new QSlider(Qt::Horizontal, parent);
			slider->setRange(mMinValue, mMaxValue); // 鏁存暟鑼冨洿
			QObject::connect(slider, &QSlider::valueChanged, [this](int v) {
				this->setData<int>(v);
			});
			return slider;
		}
	private:
		int mMinValue;
		int mMaxValue;
	};
	class InputString :public NodeData<std::string> {
	public:
		InputString(const std::string& s, const char* n) :NodeData(s, n) {
			mValueChange = false;
		}
		virtual bool valueChange() {
			return mValueChange;
		}
	   
	private:
		bool mValueChange;
	};
	template <typename T>
	class DataSetting :public NodeData<T> {
	public:
		DataSetting(T&value,const char* n):NodeData<T>(value,n)
		{
			dispatcher.RegisterReference(value);
		}
		DataSetting(std::function<void(T)>provider,std::function<T(void)>gather,const char* n) :
		NodeData(T(),n)
		{
			dispatcher.RegisterGather(gather);
			dispatcher.RegisterProvider(provider);
		}
	protected:
		DataDispatcher<T>dispatcher;
	};
	class BoolSetting :public DataSetting<bool>
	{
	public:
		BoolSetting(bool& value, const char* name) :DataSetting(value, name) {

		}
	};
	template<typename T>
	class RangeSetting :public DataSetting<T> {
	public:
		RangeSetting(T& value, T minV, T maxV, const char* n) :
			DataSetting<T>(value,n),minValue(minV),maxValue(maxV)
			
		{

		}
	protected:
		T minValue;
		T maxValue;
	};
	class RangeFLoat :public RangeSetting<float> {
	public:
		RangeFLoat(float& v, float minV, float maxV, const char* n):RangeSetting(v,minV,maxV,n) {

		}

	};
	class RangeDouble :public RangeSetting<double> {
	public:
		RangeDouble(double& v, double minV, double maxV, const char* n) :RangeSetting(v, minV, maxV, n) {

		}

	};

	template <typename T,size_t size>
	class RangeSettingN :public NodeData<std::array<T, size>> {
	public:
		RangeSettingN(const std::array<T,size>& v,T a,T b,const char* n):
		NodeData(v,n),
			mMinValue(a), mMaxValue(b)
		{
			
		}
	protected:
		T mMinValue;
		T mMaxValue;
	};
	template <size_t size>
	class RangeSettingNFloat :public RangeSettingN<float, size> {
	public:
		RangeSettingNFloat(const std::array<float, size>& v, float a, float b, const char* n)
			:RangeSettingN<float, size>(v,a,b,n)
		{
			

		}
		virtual void draw() override{
		

			ImGui::SliderScalarN(RangeSettingN<float, size>::mName.c_str(),
				ImGuiDataType_Float, RangeSettingN<float, size>::data.data(),size,
				&RangeSettingN<float, size>::mMinValue,
				&RangeSettingN<float, size>::mMaxValue);
		}
	};
	class ColorSetting :public NodeData<float*> {
	public:
		ColorSetting(float col[3],const char* n) :NodeData(col,n){

		}

	};
	DebugSettings& DebugSettings::instance()
	{
		static DebugSettings instance;
		return instance;
	}
	bool DebugSettings::add(const std::string& group, NodeBase* ptr)
	{
		if (mIndexMap.find(ptr->getName()) != mIndexMap.end()) {
			return false;
		}
		mIndexMap[ptr->getName()] = mNextIndex;
		mGroup[group].push_back(mNextIndex++);
		mRegistry.push_back(ptr);
		return true;
	}
	bool DebugSettings::registerFloatReference(const std::string& group, const std::string& key, float& value, float minValue, float maxValue)
	{
		if (mIndexMap.find(key) != mIndexMap.end()) {
			return false;
		}
		RangeFLoat* ptr = new RangeFLoat(value,minValue,maxValue,key.c_str());
		mIndexMap[ptr->getName()] = mNextIndex;
		mGroup[group].push_back(mNextIndex++);
		mRegistry.push_back(ptr);
		return true;
	}
	bool DebugSettings::registerDoubleReference(const std::string& group, const std::string& key, double& value, double minValue, double maxValue)
	{
		if (mIndexMap.find(key) != mIndexMap.end()) {
			return false;
		}
		RangeDouble* ptr = new RangeDouble(value, minValue, maxValue, key.c_str());
		mIndexMap[ptr->getName()] = mNextIndex;
		mGroup[group].push_back(mNextIndex++);
		mRegistry.push_back(ptr);
		return true;
	}
	bool DebugSettings::registerColorEdit(const std::string& group, const std::string& key, float color[3])
	{
		if (mIndexMap.find(key) != mIndexMap.end()) {
			return false;
		}
		ColorSetting* ptr = new ColorSetting(color,key.c_str());
		mIndexMap[ptr->getName()] = mNextIndex;
		mGroup[group].push_back(mNextIndex++);
		mRegistry.push_back(ptr);
		return true;
	}
	bool DebugSettings::registerBoolReference(const std::string& group, const std::string& key, bool& value)
	{
		if (mIndexMap.find(key) != mIndexMap.end()) {
			return false;
		}
		BoolSetting* ptr = new BoolSetting(value, key.c_str());
		mIndexMap[ptr->getName()] = mNextIndex;
		mGroup[group].push_back(mNextIndex++);
		mRegistry.push_back(ptr);
		return true;
	}
	bool DebugSettings::addCallBack(const std::string& key, const std::string& name,std::function<void(NodeBase* self)> fn)
	{
		auto it = mIndexMap.find(key);
		if (it == mIndexMap.end()) {
            return false;
		}
		mRegistry[it->second]->addCallBack(name,fn);
		return true;
	}
	NodeBase* DebugSettings::getNode(const std::string& key)
	{
		auto it = mIndexMap.find(key);
		if (it == mIndexMap.end()) {
			return nullptr;
		}
		return mRegistry[it->second];
	}
	DebugSettings::~DebugSettings()
	{
		for (auto& item : mRegistry) {
			delete item;
		}
	}

	std::unordered_map<std::string, std::vector<int>>& DebugSettings::getGroup()
	{
		return mGroup;
	}
	std::vector<NodeBase*>& DebugSettings::getRegistry()
	{
		return mRegistry;
	}
	DebugSettings::DebugSettings()
	{
		mHashCode[typeid(bool).hash_code()] = "bool";
		mHashCode[typeid(int).hash_code()] = "int";
		mHashCode[typeid(float).hash_code()] = "float";
		add("View","showLight",false);
		add("View", "showGrid", false);
		add("View", "showBvh", false);
		add("View", "debugElements", false);
		add("View", "BvhRayHit", true);
		add("View", "DebugImgui", false);
		add("View", "showFPS", false);
		add("View", new DragFloat(0.5, 0.5, 10.0, "zoom speed"));
		add("PathTracing", "PathTrace", false);
		add("PathTracing", "reBuildBvh", false);
		add("PathTracing", "Denoise", true);
		
		add("PathTracing", "enableRR", true);
		add("PathTracing", "enableTonemap", true);
		add("PathTracing", "enableAces", false);
		add("PathTracing", "openglNormalMap", true);
		add("PathTracing", "enableEnvMap", true);
		add("PathTracing", "enableUniformLight", false);
		add("PathTracing", "hideEmitters", false);
		add("PathTracing", "enableBackground", false);
		add("PathTracing", "transparentBackground", false);

		add("PathTracing", "independentRenderSize", false);
		add("PathTracing", "enableRoughnessMollification", false);
		add("PathTracing", "enableVolumeMIS", false);
		
		add("PathTracing", "optAlphaTest", false);
		add("PathTracing", "optMedium", false);
		
		add("PathTracing", new DragInt(10, 5, 30, "denoiserFrameCnt"));
		add("PathTracing",new DragFloat(1.0f,0.5f,3.0f,"envMapIntensity"));
		add("PathTracing",new DragFloat(0.0f,0.0f,1.0f,"roughnessMollificationAmt"));

		
	}
	bool NodeBase::valueChange()
	{
		return true;
	}

	QWidget* NodeBase::createWidget(QWidget* parent)
	{
		std::string type = getType();
		if (type == "bool") {
			auto check=new NodeBoolWidget(this, parent);
			check->setCheckValue(this->getData<bool>());
			return check;
		}
		else if (type=="int") {

		}
		else if (type == "float") {

		}
		return nullptr;
	}

	std::string NodeBase::getType()
	{
		return mHashCode[mType];
	}
	void NodeBase::submitCallBack()
	{
		for (const auto& call : mCallList) {
			createCallBack(CallBackManager::instance(),call.second,this);
		}
	}
}
