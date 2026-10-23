#pragma once
#include <ThirdParty/imgui/imgui/imgui.h>
#include "implot.h"

namespace ImPlotCustom {

    // 2D 位置+旋转变换结构体（适配 ImGui 自定义绘制）
    // 规范命名：Transform 而非 TransForm，符合C++大驼峰规范
    struct Transform
    {
        // 位置坐标
        ImVec2 pos;
        // 旋转弧度（内部统一存储弧度，避免反复转换）
        float angleRad;

        // 默认构造：归零
        Transform() : pos(ImVec2(0, 0)), angleRad(0.0f) {}

        // 构造函数：传入XY坐标 + 角度(度数)
        Transform(float x, float y, float degree)
        {
            pos.x = x;
            pos.y = y;
            setDegree(degree);
        }

        // 构造函数：传入坐标向量 + 角度(度数)
        Transform(const ImVec2& p, float degree)
        {
            pos = p;
            setDegree(degree);
        }

        // 设置坐标
        void setPos(float x, float y)
        {
            pos.x = x;
            pos.y = y;
        }

        // 设置坐标（向量重载）
        void setPos(const ImVec2& p)
        {
            pos = p;
        }

        // 设置旋转角度（传入角度，自动转弧度）
        void setDegree(float degree)
        {
            angleRad = degree / 180.0f * 3.1415926535f;
        }

        // 获取当前角度（度数）
        float getDegree() const
        {
            return angleRad * 180.0f / 3.1415926535f;
        }

        // 获取三角函数（缓存计算，避免重复求值）
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
