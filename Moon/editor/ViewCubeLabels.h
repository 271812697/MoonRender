#pragma once

#include <QImage>
#include <QString>

namespace MOON
{
	/** The six labels of the view cube, drawn into one atlas at runtime.
	 *
	 * The cube used to sample a hand-made PNG (XYZ.png) that had the six words baked in: the text
	 * was soft at any scale it was not authored for, and a second language meant a second picture.
	 * The atlas is the same 3x2 layout the cube's UVs expect, only painted here instead - the text
	 * is rendered at the size it is displayed at, follows the UI language and can be regenerated
	 * whenever either changes.
	 *
	 * The layout (column, row) of each face is fixed by the cube's UVs:
	 *   (0,0) Left   (1,0) Down   (2,0) Back
	 *   (0,1) Right  (1,1) Up     (2,1) Front
	 *
	 * "Back" is turned upside down because that face is looked at from the other side of the
	 * cube, which is how the baked atlas had it too. */
	namespace ViewCubeLabels
	{
		/** Builds the atlas. p_cellPixels is the size of one face; the image is 3 cells wide and 2
		 * tall. Labels are drawn in the current UI language. */
		QImage BuildAtlas(int p_cellPixels);

		/** Normalised UV rectangle of one face inside the atlas: x, y, width, height in 0..1.
		 * The atlas painter and the cube UVs both read this single table, so the picture and
		 * what the geometry samples cannot drift apart. Face order matches NavigateCube():
		 * front, down, back, up, right, left. */
		void FaceUvRect(int p_face, float& p_x, float& p_y, float& p_w, float& p_h);

		/** Marks the atlas as out of date (language or theme changed); the renderer uploads it
		 * again on the next frame. */
		void Invalidate();
		bool IsDirty();
		/** Cleared once the texture has been uploaded. */
		void MarkClean();
	}
}
