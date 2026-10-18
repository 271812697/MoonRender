#pragma once
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
//#define IMOGizmo_LEFT_HANDED
#include "imGuizmo.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
namespace ImGizmo {
	namespace internal {
		static struct Config {
			float mX = 50.f;
			float mY = 50.f;
			float mSize = 100.f;
			ImDrawList* mDrawList = nullptr;
		} config;

		struct ImVec3
		{
			ImVec3(const float x, const float y, const float z) : mData{ x, y, z } {}
			explicit ImVec3(const float* const data) : mData{ data[0], data[1], data[2] } {}
			float operator[](const int idx) const { return mData[idx]; }
			ImVec3 operator+(const ImVec3& other) const { return { mData[0] + other[0], mData[1] + other[1], mData[2] + other[2] }; }
			ImVec3 operator-(const ImVec3& other) const { return { mData[0] - other[0], mData[1] - other[1], mData[2] - other[2] }; }
			ImVec3 operator*(const float scalar) const { return { mData[0] * scalar, mData[1] * scalar, mData[2] * scalar }; }
			ImVec3 operator*(const ImVec3& other) const { return { mData[0] * other[0], mData[1] * other[1], mData[2] * other[2] }; }
			float mData[3];
		};
		ImVec4 multiply(const float* const m, const ImVec4& v)
		{
			const float x = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w;
			const float y = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w;
			const float z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w;
			const float w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w;
			return { x, y, z, w };
		}

		void multiply(const float* const l, const float* const r, float* out)
		{
			out[0] = l[0] * r[0] + l[1] * r[4] + l[2] * r[8] + l[3] * r[12];
			out[1] = l[0] * r[1] + l[1] * r[5] + l[2] * r[9] + l[3] * r[13];
			out[2] = l[0] * r[2] + l[1] * r[6] + l[2] * r[10] + l[3] * r[14];
			out[3] = l[0] * r[3] + l[1] * r[7] + l[2] * r[11] + l[3] * r[15];

			out[4] = l[4] * r[0] + l[5] * r[4] + l[6] * r[8] + l[7] * r[12];
			out[5] = l[4] * r[1] + l[5] * r[5] + l[6] * r[9] + l[7] * r[13];
			out[6] = l[4] * r[2] + l[5] * r[6] + l[6] * r[10] + l[7] * r[14];
			out[7] = l[4] * r[3] + l[5] * r[7] + l[6] * r[11] + l[7] * r[15];

			out[8] = l[8] * r[0] + l[9] * r[4] + l[10] * r[8] + l[11] * r[12];
			out[9] = l[8] * r[1] + l[9] * r[5] + l[10] * r[9] + l[11] * r[13];
			out[10] = l[8] * r[2] + l[9] * r[6] + l[10] * r[10] + l[11] * r[14];
			out[11] = l[8] * r[3] + l[9] * r[7] + l[10] * r[11] + l[11] * r[15];

			out[12] = l[12] * r[0] + l[13] * r[4] + l[14] * r[8] + l[15] * r[12];
			out[13] = l[12] * r[1] + l[13] * r[5] + l[14] * r[9] + l[15] * r[13];
			out[14] = l[12] * r[2] + l[13] * r[6] + l[14] * r[10] + l[15] * r[14];
			out[15] = l[12] * r[3] + l[13] * r[7] + l[14] * r[11] + l[15] * r[15];
		}

		inline float dot(const ImVec3& a, const ImVec3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		inline ImVec3 cross(const ImVec3& a, const ImVec3& b)
		{
			return {
				a[1] * b[2] - a[2] * b[1],
				a[2] * b[0] - a[0] * b[2],
				a[0] * b[1] - a[1] * b[0]
			};
		}

		inline ImVec3 normalize(const ImVec3& a)
		{
			float il = 1.f / (sqrtf(dot(a, a)) + FLT_EPSILON);
			return a * il;
		}

		inline bool checkInsideCircle(const ImVec2 center, const float radius, const ImVec2 point)
		{
			return (point.x - center.x) * (point.x - center.x) + (point.y - center.y) * (point.y - center.y) <= radius * radius;
		}

		void drawPositiveLine(const ImVec2& center, const ImVec2& axis, const ImU32& color, const float thickness, const char* text) {
			const auto lineEndPositive = ImVec2{ center.x + axis.x, center.y + axis.y };
			internal::config.mDrawList->AddLine(center, lineEndPositive, color, thickness);
			const auto nor = ImVec2{ -axis.y,axis.x };
			float len = 1.5 * thickness / sqrtf(nor.x * nor.x + nor.y * nor.y);

			if (axis.x * axis.x + axis.y * axis.y > FLT_EPSILON) {
				internal::config.mDrawList->AddTriangleFilled(lineEndPositive + nor * len, lineEndPositive - nor * len, lineEndPositive + axis * len * 3.46, color);
			}
			else
			{
				len = 0;
			}
			//internal::config.mDrawList->AddCircleFilled(lineEndPositive, radius, color);
			const auto labelSize = ImGui::CalcTextSize(text);
			const auto corner = lineEndPositive - nor * len * 2.0;
			const auto textPos = ImVec2(floor(corner.x - 0.5f * labelSize.x), floor(corner.y - 0.5f * labelSize.y));
			internal::config.mDrawList->AddText(textPos, color, text);
		}

		void drawNegativeLine(const ImVec2& center, const ImVec2& axis, const ImU32& color, const float radius) {
			const auto lineEndNegative = ImVec2{ center.x - axis.x, center.y - axis.y };
			internal::config.mDrawList->AddCircleFilled(lineEndNegative, radius, color);

		}

		void lookAt(ImVec3 const& eye, ImVec3 const& at, ImVec3 const& up, float* viewMatrix)
		{
#ifdef IMOGizmo_LEFT_HANDED
			const auto f = normalize(at - eye);
			const auto r = normalize(cross(up, f));
			const auto u = cross(f, r);
			viewMatrix[0] = r[0]; viewMatrix[1] = u[0]; viewMatrix[2] = f[0]; viewMatrix[3] = 0.0f;
			viewMatrix[4] = r[1]; viewMatrix[5] = u[1]; viewMatrix[6] = f[1]; viewMatrix[7] = 0.0f;
			viewMatrix[8] = r[2]; viewMatrix[9] = u[2]; viewMatrix[10] = f[2]; viewMatrix[11] = 0.0f;
			viewMatrix[12] = -dot(r, eye); viewMatrix[13] = -dot(u, eye); viewMatrix[14] = -dot(f, eye); viewMatrix[15] = 1.0f;
#else
			const auto f = normalize(at - eye);
			const auto r = normalize(cross(f, up));
			const auto u = cross(r, f);
			viewMatrix[0] = r[0]; viewMatrix[1] = u[0]; viewMatrix[2] = -f[0]; viewMatrix[3] = 0.0f;
			viewMatrix[4] = r[1]; viewMatrix[5] = u[1]; viewMatrix[6] = -f[1]; viewMatrix[7] = 0.0f;
			viewMatrix[8] = r[2]; viewMatrix[9] = u[2]; viewMatrix[10] = -f[2]; viewMatrix[11] = 0.0f;
			viewMatrix[12] = -dot(r, eye); viewMatrix[13] = -dot(u, eye); viewMatrix[14] = dot(f, eye); viewMatrix[15] = 1.0f;
#endif
		}

		void invert4x4(const float* m, float* out)
		{
			out[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
			out[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
			out[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
			out[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
			out[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
			out[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
			out[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
			out[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
			out[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
			out[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
			out[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
			out[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
			out[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
			out[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
			out[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
			out[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

			float det = m[0] * out[0] + m[1] * out[4] + m[2] * out[8] + m[3] * out[12];
			det = 1.0f / det;
			for (unsigned int i = 0; i < 16; i++) out[i] = out[i] * det;
		}
	}

	static struct Config {
		// in relation to half the rect size
		float lineThicknessScale = 0.034f;
		float axisLengthScale = 0.33f;
		float positiveRadiusScale = 0.075f;
		float negativeRadiusScale = 0.05f;
		float hoverCircleRadiusScale = 0.88f;
		ImU32 xCircleFrontColor = IM_COL32(255, 0, 0, 255);
		ImU32 xCircleBackColor = IM_COL32(200, 0, 0, 255);
		ImU32 yCircleFrontColor = IM_COL32(0, 255, 0, 255);
		ImU32 yCircleBackColor = IM_COL32(0, 200, 0, 255);
		ImU32 zCircleFrontColor = IM_COL32(0, 0, 255, 255);
		ImU32 zCircleBackColor = IM_COL32(0, 0, 200, 255);
		ImU32 hoverCircleColor = IM_COL32(100, 100, 100, 130);
	} config;

	void SetRect(const float x, const float y, const float size)
	{
		internal::config.mX = x;
		internal::config.mY = y;
		internal::config.mSize = size;
	}

	void SetDrawList(ImDrawList* drawlist)
	{
		internal::config.mDrawList = ImGui::GetForegroundDrawList();;
		///internal::config.mDrawList = drawlist ? drawlist : ImGui::GetWindowDrawList();
	}

	void BeginFrame(const bool background)
	{
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ((background != true) ? ImGuiWindowFlags_NoBackground : ImGuiWindowFlags_None);
		ImGui::SetNextWindowPos({ internal::config.mX, internal::config.mY }, ImGuiCond_Always);
		ImGui::SetNextWindowSize({ internal::config.mSize, internal::config.mSize });
		ImGui::Begin("imoGizmo", nullptr, flags);
		SetDrawList(internal::config.mDrawList);
		ImGui::End();
	}

	bool DrawGizmo(float* const viewMatrix, const float* const projectionMatrix, const float pivotDistance) {
		const float size = internal::config.mSize;
		const float hSize = size * 0.5f;
		const auto center = ImVec2{ internal::config.mX + hSize, internal::config.mY + hSize };
		float viewProjection[16];
		internal::multiply(viewMatrix, projectionMatrix, viewProjection);
		// correction for non-square aspect ratio
		{
			const float aspectRatio = projectionMatrix[5] / projectionMatrix[0];
			viewProjection[0] *= aspectRatio; viewProjection[8] *= aspectRatio;
		}
		// axis
		const float axisLength = size * config.axisLengthScale;
		ImVec4 xAxis = internal::multiply(viewProjection, ImVec4{ axisLength, 0, 0, 0 });
		ImVec4 yAxis = internal::multiply(viewProjection, ImVec4{ 0, axisLength, 0, 0 });
		ImVec4 zAxis = internal::multiply(viewProjection, ImVec4{ 0, 0, axisLength, 0 });
		ImVec2 x = { xAxis.x, -xAxis.y };
		ImVec2 y = { yAxis.x, -yAxis.y };
		ImVec2 z = { zAxis.x, -zAxis.y };

		SetDrawList(internal::config.mDrawList);
		const float positiveRadius = size * config.positiveRadiusScale;
		const float negativeRadius = size * config.negativeRadiusScale;
		// draw back first
		const float lineThickness = size * config.lineThicknessScale;
		// +x axis
		internal::drawPositiveLine(center, x, config.xCircleFrontColor, lineThickness, "X");
		// +y axis
		internal::drawPositiveLine(center, y, config.yCircleFrontColor, lineThickness, "Y");
		// +z axis
		internal::drawPositiveLine(center, z, config.zCircleFrontColor, lineThickness, "Z");
		// -x axis
		internal::drawNegativeLine(center, x, config.xCircleFrontColor, negativeRadius);
		// -y axis
		internal::drawNegativeLine(center, y, config.yCircleFrontColor, negativeRadius);
		// -z axis
		internal::drawNegativeLine(center, z, config.zCircleFrontColor, negativeRadius);
		internal::config.mDrawList->AddCircleFilled(center, negativeRadius + 1, IM_COL32_WHITE);
		//ImVec2 points[4] = { center,center + x,center + x + y,center + y };
		//internal::config.mDrawList->AddConvexPolyFilled(points, 4, IM_COL32(0, 0, 255, 100));
		internal::config.mDrawList = nullptr;
		return false;
	}
}