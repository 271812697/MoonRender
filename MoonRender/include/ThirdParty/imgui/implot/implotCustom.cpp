#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "implotCustom.h"
#include "implot_internal.h"
#include <algorithm>
#include <cmath>
#include <vector>
using namespace ImPlot;
namespace ImPlotCustom {
	//y axis is down
	static ImVec2 RotateVec2(ImVec2 pos, float cosVal, float sinVal) {
		return ImVec2(pos.x * cosVal + pos.y * sinVal, -pos.x * sinVal + pos.y * cosVal);
	}
	void Transform::getSinCos(float& outSin, float& outCos) const
	{
		outSin = sinf(angleRad);
		outCos = cosf(angleRad);
	}
	ImVec2 Transform::value(float x, float y)const
	{
		float sinVal, cosVal;
		getSinCos(sinVal, cosVal);
		return RotateVec2(ImVec2(x, y), cosVal, sinVal) + pos;
	}
	ImVec2 Transform::value(ImVec2 vec2)const
	{
		float sinVal, cosVal;
		getSinCos(sinVal, cosVal);
		return RotateVec2(vec2, cosVal, sinVal) + pos;
	}
	static ImDrawList* getDrawList(){
		// Draw lists are reset every frame; never cache the pointer across
		// frames or ImGui contexts.
		return ImGui::GetForegroundDrawList();
	}

	static void RenderArrowsForVerticalBar(ImDrawList* draw_list, ImVec2 pos, ImVec2 half_sz, float bar_w, float alpha)
	{
		ImU32 alpha8 = IM_F32_TO_INT8_SAT(alpha);
		ImGui::RenderArrowPointingAt(draw_list, ImVec2(pos.x + half_sz.x + 1, pos.y), ImVec2(half_sz.x + 2, half_sz.y + 1), ImGuiDir_Right, IM_COL32(0, 0, 0, alpha8));
		ImGui::RenderArrowPointingAt(draw_list, ImVec2(pos.x + half_sz.x, pos.y), half_sz, ImGuiDir_Right, IM_COL32(255, 255, 255, alpha8));
		ImGui::RenderArrowPointingAt(draw_list, ImVec2(pos.x + bar_w - half_sz.x - 1, pos.y), ImVec2(half_sz.x + 2, half_sz.y + 1), ImGuiDir_Left, IM_COL32(0, 0, 0, alpha8));
		ImGui::RenderArrowPointingAt(draw_list, ImVec2(pos.x + bar_w - half_sz.x, pos.y), half_sz, ImGuiDir_Left, IM_COL32(255, 255, 255, alpha8));
	}

	void testImPlotCustom()
	{

		return;
		ImPlotCustom::ColormapScale("lo", 1.0, 0.0, 10.0);
		AddArrow(Transform(100, 100, 45), IM_COL32(255, 0, 255, 255), 50, 2);
		drawDoubleArrow(Transform(100, 100, -60), IM_COL32(255, 0, 255, 255), 180, 2, "10.mm");
		ImPlot::ShowDemoWindow();
		ImGui::ShowDemoWindow();
	}
	void AddTextTransform(const Transform& trans, ImU32 col, const char* text_begin, const char* text_end)
	{
		ImDrawList* DrawList = getDrawList();
		if (!text_end)
			text_end = text_begin + strlen(text_begin);

		ImGuiContext& g = *GImGui;
#ifdef IMGUI_HAS_TEXTURES
		ImFontBaked* font = g.Font->GetFontBaked(g.FontSize);
		const float scale = g.FontSize / font->Size;
#else
		ImFont* font = g.Font;
		const float scale = g.FontSize / font->FontSize;
#endif
		Transform transCopy = trans;

		float cosVal, sinVal;
		trans.getSinCos(sinVal, cosVal);

		const char* s = text_begin;
		int chars_exp = (int)(text_end - s);
		int chars_rnd = 0;
		const int vtx_count_max = chars_exp * 4;
		const int idx_count_max = chars_exp * 6;
		DrawList->PrimReserve(idx_count_max, vtx_count_max);

		while (s < text_end)
		{
			unsigned int c = (unsigned int)*s;
			if (c < 0x80)
			{
				s += 1;
			}
			else
			{
				s += ImTextCharFromUtf8(&c, s, text_end);
				if (c == 0)
					break;
			}

			const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
			if (glyph == nullptr)
				continue;
			
			ImVec2 p0 = transCopy.value(ImVec2(glyph->X0, glyph->Y0) * scale);
			ImVec2 p1 = transCopy.value(ImVec2(glyph->X1, glyph->Y0) * scale);
			ImVec2 p2 = transCopy.value(ImVec2(glyph->X1, glyph->Y1) * scale);
			ImVec2 p3 = transCopy.value(ImVec2(glyph->X0, glyph->Y1) * scale);

			DrawList->PrimQuadUV(p0,p1 ,p2 ,p3,
				ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V0),
				ImVec2(glyph->U1, glyph->V1), ImVec2(glyph->U0, glyph->V1),
				col);
			transCopy.move(glyph->AdvanceX * scale * cosVal,-glyph->AdvanceX * scale * sinVal);
			chars_rnd++;
		}

		int chars_skp = chars_exp - chars_rnd;
		DrawList->PrimUnreserve(chars_skp * 6, chars_skp * 4);

	}

	void AddArrow(const Transform& trans, ImU32 col, float length, float thickNess)
	{
		ImDrawList* drawList = getDrawList();
		float scale =2.5 * thickNess;
		ImVec2 s=trans.value(0.0f, 0.0f);
		ImVec2 e = trans.value(length,0.0f );
		ImVec2 a = trans.value(length, scale);
		ImVec2 b = trans.value(length, -scale);
		ImVec2 c = trans.value(length+2*scale, 0);
		drawList->AddLine(s, e, col, thickNess);
		drawList->AddTriangleFilled(a,b,c,col);
	}

	void drawDoubleArrow(const Transform& trans, ImU32 col, float length, float thickNess, const char* text)
	{
		
		ImDrawList* drawList = getDrawList();
		float scale = 4.0f * thickNess;
		ImVec2 s = trans.value(0.0f, 0.0f);
		ImVec2 e = trans.value(length, 0.0f);
		// Arrow heads are drawn BETWEEN the two endpoints: each triangle's tip
		// is exactly at an endpoint and its base is inset by one head length.
		ImVec2 a = trans.value(length - 2 * scale, scale);
		ImVec2 b = trans.value(length - 2 * scale, -scale);
		ImVec2 c = trans.value(length, 0);
		ImVec2 a1 = trans.value(0, 0);
		ImVec2 b1 = trans.value(2 * scale, scale);
		ImVec2 c1 = trans.value(2 * scale, -scale);
		drawList->AddLine(s, e, col, thickNess);
		drawList->AddTriangleFilled(a, b, c, col);
		drawList->AddTriangleFilled(a1, b1, c1, col);
		if (text && text[0]) {
			const ImVec2 labelSize = ImGui::CalcTextSize(text, nullptr, true);
			if (labelSize.x > 0.0f && labelSize.y > 0.0f) {
				// The label is always drawn horizontally (screen-aligned), not
				// rotated with the shaft. Center it on the shaft midpoint and
				// keep it just above the line.
				ImGuiContext& g = *GImGui;
				float descent;
#ifdef IMGUI_HAS_TEXTURES
				ImFontBaked* bakedFont = g.Font->GetFontBaked(g.FontSize);
				const float fontScale = g.FontSize / bakedFont->Size;
				descent = bakedFont->Descent * fontScale;
#else
				// Legacy fonts do not expose ascent/descent; approximate the
				// descent with the usual 20% of the measured text height.
				descent = labelSize.y * 0.20f;
#endif
				const float gap = 3.0f;  // pixels between the line and the text

				// Baseline offset so the glyph box bottom sits one gap above
				// the shaft (screen Y grows downwards).
				const ImVec2 shaftMid((s.x + e.x) * 0.5f, (s.y + e.y) * 0.5f);
				const float anchorX = shaftMid.x - labelSize.x * 0.5f;
				const float anchorY = shaftMid.y - (gap + descent);

				AddTextTransform(Transform(anchorX, anchorY, 0.0f), col, text);
			}
		}
	}

	// Arc dimension double arrow.
	// Convention: trans.pos is the arc center, trans.angleRad is the start
	// angle (Transform stores radians), "degree" is the swept angle in degrees
	// (may be negative). The two arrow tips sit exactly at the arc endpoints;
	// the optional label is drawn horizontally at the outer side of the arc
	// middle.
	void drawDoubleArcArrow(const Transform& trans, ImU32 col, float radius, float degree, float thickNess, const char* text)
	{
		ImDrawList* drawList = getDrawList();
		const float pi = 3.14159265358979f;
		const float sweep = degree * pi / 180.0f;
		if (radius <= 0.01f || fabsf(sweep) < 1e-3f) {
			return;
		}

		const float a0 = trans.angleRad;
		const float a1 = a0 + sweep;
		const float cosA0 = cosf(a0), sinA0 = sinf(a0);
		const float cosA1 = cosf(a1), sinA1 = sinf(a1);

		// Approximate the arc with a polyline.
		const int segCount = std::max(8, static_cast<int>(ceilf(fabsf(degree) / 5.0f)));
		std::vector<ImVec2> arcPoints;
		arcPoints.reserve(segCount + 1);
		for (int i = 0; i <= segCount; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(segCount);
			const float a = a0 + (a1 - a0) * t;
			arcPoints.emplace_back(
				trans.pos.x + radius * cosf(a),
				trans.pos.y + radius * sinf(a)
			);
		}
		drawList->AddPolyline(arcPoints.data(), static_cast<int>(arcPoints.size()), col, 0, thickNess);

		// Arrow heads with their tips exactly on the arc endpoints.
		// Tangent direction along increasing angle: T = (-sin a, cos a).
		const float scale = 4.0f * thickNess;
		const float head = 2.0f * scale;
		auto drawHead = [&](const ImVec2& tip, const ImVec2& dirOut) {
			// dirOut is the unit direction from the base towards the tip.
			const ImVec2 base(tip.x - dirOut.x * head, tip.y - dirOut.y * head);
			const ImVec2 perp(-dirOut.y, dirOut.x);
			const ImVec2 corner0(base.x + perp.x * scale, base.y + perp.y * scale);
			const ImVec2 corner1(base.x - perp.x * scale, base.y - perp.y * scale);
			drawList->AddTriangleFilled(corner0, corner1, tip, col);
		};

		const ImVec2 startTip(trans.pos.x + radius * cosA0, trans.pos.y + radius * sinA0);
		const ImVec2 endTip(trans.pos.x + radius * cosA1, trans.pos.y + radius * sinA1);
		// Like the straight double arrow, the heads point outward: tips sit on
		// the arc endpoints, bases are inside the arc. The travel direction
		// reverses for a negative sweep, so mirror both heads by its sign.
		const float sweepSign = (sweep >= 0.0f) ? 1.0f : -1.0f;
		drawHead(startTip, ImVec2(sinA0 * sweepSign, -cosA0 * sweepSign));
		drawHead(endTip, ImVec2(-sinA1 * sweepSign, cosA1 * sweepSign));

		if (text && text[0]) {
			const ImVec2 labelSize = ImGui::CalcTextSize(text, nullptr, true);
			if (labelSize.x > 0.0f && labelSize.y > 0.0f) {
				ImGuiContext& g = *GImGui;
				float ascent, descent;
#ifdef IMGUI_HAS_TEXTURES
				ImFontBaked* bakedFont = g.Font->GetFontBaked(g.FontSize);
				const float fontScale = g.FontSize / bakedFont->Size;
				ascent = bakedFont->Ascent * fontScale;
				descent = bakedFont->Descent * fontScale;
#else
				ascent = labelSize.y * 0.80f;
				descent = labelSize.y * 0.20f;
#endif
				const float gap = 3.0f;
				const float aMid = (a0 + a1) * 0.5f;
				const float outwardX = cosf(aMid);
				const float outwardY = sinf(aMid);
				// Center the (horizontal) label on the outer side of the arc.
				const float centerDist = radius + labelSize.y * 0.5f + gap;
				const float labelCX = trans.pos.x + outwardX * centerDist;
				const float labelCY = trans.pos.y + outwardY * centerDist;
				// Convert visual center to a baseline offset.
				const float baselineY = labelCY + (ascent - descent) * 0.5f;
				const float anchorX = labelCX - labelSize.x * 0.5f;

				AddTextTransform(Transform(anchorX, baselineY, 0.0f), col, text);
			}
		}
	}
	
	void ColormapScale(const char* label, double val, double scale_min, double scale_max, const ImVec2& pos, const ImVec2& size, const char* format, ImPlotColormapScaleFlags flags, ImPlotColormap cmap)
	{
		ImGuiContext& G = *GImGui;

		ImVec2 label_size(0, 0);
		if (!ImHasFlag(flags, ImPlotColormapScaleFlags_NoLabel)) {
			label_size = ImGui::CalcTextSize(label, nullptr, true);
		}

		ImPlotContext& gp = *GImPlot;
		cmap = cmap == IMPLOT_AUTO ? gp.Style.Colormap : cmap;
		IM_ASSERT_USER_ERROR(cmap >= 0 && cmap < gp.ColormapData.Count, "Invalid colormap index!");

		ImVec2 frame_size = ImGui::CalcItemSize(size, 0, gp.Style.PlotDefaultSize.y);
		if (frame_size.y < gp.Style.PlotMinSize.y && size.y < 0.0f)
			frame_size.y = gp.Style.PlotMinSize.y;

		ImPlotRange range(ImMin(scale_min, scale_max), ImMax(scale_min, scale_max));
		gp.CTicker.Reset();
		Locator_Default(gp.CTicker, range, frame_size.y, true, Formatter_Default, (void*)format);

		const bool rend_label = label_size.x > 0;
		const float txt_off = gp.Style.LabelPadding.x;
		const float pad = txt_off + gp.CTicker.MaxSize.x + (rend_label ? txt_off + label_size.y : 0);
		float bar_w = 20;
		if (frame_size.x == 0)
			frame_size.x = bar_w + pad + 2 * gp.Style.PlotPadding.x;
		else {
			bar_w = frame_size.x - (pad + 2 * gp.Style.PlotPadding.x);
			if (bar_w < gp.Style.MajorTickLen.y)
				bar_w = gp.Style.MajorTickLen.y;
		}

		ImDrawList& DrawList = *ImGui::GetForegroundDrawList();
		ImRect bb_frame = ImRect(pos, pos + frame_size);



		const bool opposite = ImHasFlag(flags, ImPlotColormapScaleFlags_Opposite);
		const bool inverted = ImHasFlag(flags, ImPlotColormapScaleFlags_Invert);
		const bool reversed = scale_min > scale_max;

		float bb_grad_shift = opposite ? pad : 0;
		ImRect bb_grad(bb_frame.Min + gp.Style.PlotPadding + ImVec2(bb_grad_shift, 0),
			bb_frame.Min + ImVec2(bar_w + gp.Style.PlotPadding.x + bb_grad_shift,
				frame_size.y - gp.Style.PlotPadding.y));

		const ImU32 col_text = ImGui::GetColorU32(ImGuiCol_Text);
		const bool invert_scale = inverted ? (reversed ? false : true) : (reversed ? true : false);
		const float y_min = invert_scale ? bb_grad.Max.y : bb_grad.Min.y;
		const float y_max = invert_scale ? bb_grad.Min.y : bb_grad.Max.y;

		RenderColorBar(gp.ColormapData.GetKeys(cmap), gp.ColormapData.GetKeyCount(cmap), DrawList, bb_grad, true, !inverted, !gp.ColormapData.IsQual(cmap));
		for (int i = 0; i < gp.CTicker.TickCount(); ++i) {
			const double y_pos_plt = gp.CTicker.Ticks[i].PlotPos;
			const float y_pos = ImRemap((float)y_pos_plt, (float)range.Max, (float)range.Min, y_min, y_max);
			const float tick_width = gp.CTicker.Ticks[i].Major ? gp.Style.MajorTickLen.y : gp.Style.MinorTickLen.y;
			const float tick_thick = gp.CTicker.Ticks[i].Major ? gp.Style.MajorTickSize.y : gp.Style.MinorTickSize.y;
			const float tick_t = (float)((y_pos_plt - scale_min) / (scale_max - scale_min));
			const ImU32 tick_col = CalcTextColor(gp.ColormapData.LerpTable(cmap, tick_t));
			if (y_pos < bb_grad.Max.y - 2 && y_pos > bb_grad.Min.y + 2) {
				DrawList.AddLine(opposite ? ImVec2(bb_grad.Min.x + 1, y_pos) : ImVec2(bb_grad.Max.x - 1, y_pos),
					opposite ? ImVec2(bb_grad.Min.x + tick_width, y_pos) : ImVec2(bb_grad.Max.x - tick_width, y_pos),
					tick_col,
					tick_thick);
			}
			const float txt_x = opposite ? bb_grad.Min.x - txt_off - gp.CTicker.Ticks[i].LabelSize.x : bb_grad.Max.x + txt_off;
			const float txt_y = y_pos - gp.CTicker.Ticks[i].LabelSize.y * 0.5f;
			DrawList.AddText(ImVec2(txt_x, txt_y), col_text, gp.CTicker.GetText(i));
		}

		if (rend_label) {
			const float pos_x = opposite ? bb_frame.Min.x + gp.Style.PlotPadding.x : bb_grad.Max.x + 2 * txt_off + gp.CTicker.MaxSize.x;
			const float pos_y = bb_grad.GetCenter().y + label_size.x * 0.5f;
			const char* label_end = ImGui::FindRenderedTextEnd(label);
			AddTextVertical(&DrawList, ImVec2(pos_x, pos_y), col_text, label, label_end);
		}
		DrawList.AddRect(bb_grad.Min, bb_grad.Max, GetStyleColorU32(ImPlotCol_PlotBorder));
		float bars_triangles_half_sz = IM_TRUNC(bar_w * 0.20f);
		float y = bb_grad.Min.y + (bb_grad.Max.y - bb_grad.Min.y) * (scale_max - val) / (scale_max - scale_min);
		RenderArrowsForVerticalBar(&DrawList, ImVec2(bb_grad.Min.x - 1, y), ImVec2(bars_triangles_half_sz + 1, bars_triangles_half_sz), bar_w + 2.0f, 0.8);

	}

}
