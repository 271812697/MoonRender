#pragma once
#include <ThirdParty/imgui/imgui/imgui.h>
#include "implot.h"

namespace ImPlotCustom {

    // 2D 浣嶇疆+鏃嬭浆鍙樻崲缁撴瀯浣擄紙閫傞厤 ImGui 鑷畾涔夌粯鍒讹級
    // 瑙勮寖鍛藉悕锛歍ransform 鑰岄潪 TransForm锛岀鍚圕++澶ч┘宄拌鑼?
    struct Transform
    {
        // 浣嶇疆鍧愭爣
        ImVec2 pos;
        // 鏃嬭浆寮у害锛堝唴閮ㄧ粺涓€瀛樺偍寮у害锛岄伩鍏嶅弽澶嶈浆鎹級
        float angleRad;

        // 榛樿鏋勯€狅細褰掗浂
        Transform() : pos(ImVec2(0, 0)), angleRad(0.0f) {}

        // 鏋勯€犲嚱鏁帮細浼犲叆XY鍧愭爣 + 瑙掑害(搴︽暟)
        Transform(float x, float y, float degree)
        {
            pos.x = x;
            pos.y = y;
            setDegree(degree);
        }

        // 鏋勯€犲嚱鏁帮細浼犲叆鍧愭爣鍚戦噺 + 瑙掑害(搴︽暟)
        Transform(const ImVec2& p, float degree)
        {
            pos = p;
            setDegree(degree);
        }

        // 璁剧疆鍧愭爣
        void setPos(float x, float y)
        {
            pos.x = x;
            pos.y = y;
        }

        // 璁剧疆鍧愭爣锛堝悜閲忛噸杞斤級
        void setPos(const ImVec2& p)
        {
            pos = p;
        }

        // 璁剧疆鏃嬭浆瑙掑害锛堜紶鍏?瑙掑害锛岃嚜鍔ㄨ浆寮у害锛?
        void setDegree(float degree)
        {
            angleRad = degree / 180.0f * 3.1415926535f;
        }

        // 鑾峰彇褰撳墠瑙掑害锛堝害鏁帮級
        float getDegree() const
        {
            return angleRad * 180.0f / 3.1415926535f;
        }

        // 鑾峰彇涓夎鍑芥暟锛堢紦瀛樿绠楋紝閬垮厤閲嶅姹傚€硷級
        void getSinCos(float& outSin, float& outCos) const;

        void move(float x, float y) {
            pos.x += x;
            pos.y += y;
        }
		void move(const ImVec2& vec2)
		{
			pos.x += vec2.x;
			pos.y += vec2.y;
		}
        ImVec2 value(float x, float y)const;
        ImVec2 value(ImVec2 vec2)const ;
    };
	void testImPlotCustom();
	void AddTextTransform(const Transform& trans, ImU32 col, const char* text_begin, const char* text_end = nullptr);
	void AddArrow(const Transform& trans, ImU32 col, float length ,float thickNess);
	void drawDoubleArcArrow(const Transform& trans, ImU32 col, float radius, float degree, float thickNess, const char* text = nullptr);
	void drawDoubleArrow(const Transform& trans, ImU32 col, float length, float thickNess,const char*text=nullptr);
	void ColormapScale(const char* label,double val, double scale_min, double scale_max, const ImVec2& pos= ImVec2(0, 0),const ImVec2& size = ImVec2(0, 0), const char* format = "%g", ImPlotColormapScaleFlags flags = 0, ImPlotColormap cmap = IMPLOT_AUTO);
}
