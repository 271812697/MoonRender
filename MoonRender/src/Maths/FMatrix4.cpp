#include <cstring>
#include <stdexcept>
#include <string>
#include <cmath>

#include "Maths/FMatrix4.h"
#include "Maths/FVector3.h"
#include "Maths/FQuaternion.h"

constexpr float kPI = 3.14159265359f;
constexpr float kEpsilon = 0.0000001f;

const Maths::FMatrix4 Maths::FMatrix4::Identity{
	1.f, 0.f, 0.f, 0.f,
	0.f, 1.f, 0.f, 0.f,
	0.f, 0.f, 1.f, 0.f,
	0.f, 0.f, 0.f, 1.f
};

Maths::FMatrix4::FMatrix4()
{
	memcpy(this->data, Identity.data, 16 * sizeof(float)); // TODO: memcpy is not great (consider std::array)
}

Maths::FMatrix4::FMatrix4(float p_element1, float p_element2, float p_element3, float p_element4, float p_element5, float p_element6, float p_element7, float p_element8, float p_element9, float p_element10, float p_element11, float p_element12, float p_element13, float p_element14, float p_element15, float p_element16)
{
	data[0] = p_element1;
	data[1] = p_element2;
	data[2] = p_element3;
	data[3] = p_element4;
	data[4] = p_element5;
	data[5] = p_element6;
	data[6] = p_element7;
	data[7] = p_element8;
	data[8] = p_element9;
	data[9] = p_element10;
	data[10] = p_element11;
	data[11] = p_element12;
	data[12] = p_element13;
	data[13] = p_element14;
	data[14] = p_element15;
	data[15] = p_element16;
}

Maths::FMatrix4::FMatrix4(const FMatrix4& p_other)
{
	*this = p_other;
}

Maths::FMatrix4::FMatrix4(const FMatrix3& p_other) : FMatrix4(
	p_other.data[0], p_other.data[1], p_other.data[2], 0.f,
	p_other.data[3], p_other.data[4], p_other.data[5], 0.f,
	p_other.data[6], p_other.data[7], p_other.data[8], 0.f,
	0.f, 0.f, 0.f, 1.f
)
{
}

Maths::FMatrix4& Maths::FMatrix4::operator=(const FMatrix4& p_other)
{
	memcpy(this->data, p_other.data, 16 * sizeof(float));
	return *this;
}

bool Maths::FMatrix4::operator==(const FMatrix4& p_other)
{
	return AreEquals(*this, p_other);
}

Maths::FMatrix4 Maths::FMatrix4::operator+(const FMatrix4& p_other) const
{
	return Add(*this, p_other);
}

Maths::FMatrix4& Maths::FMatrix4::operator+=(const FMatrix4& p_other)
{
	*this = Add(*this, p_other);
	return *this;
}

Maths::FMatrix4 Maths::FMatrix4::operator-(float p_scalar) const
{
	return Subtract(*this, p_scalar);
}

Maths::FMatrix4 Maths::FMatrix4::operator-(const FMatrix4& p_other) const
{
	return Subtract(*this, p_other);
}

Maths::FMatrix4& Maths::FMatrix4::operator-=(const FMatrix4& p_other)
{
	*this = Subtract(*this, p_other);
	return *this;
}

Maths::FMatrix4 Maths::FMatrix4::operator*(float p_scalar) const
{
	return Multiply(*this, p_scalar);
}

Maths::FMatrix4& Maths::FMatrix4::operator*=(float p_scalar)
{
	*this = Multiply(*this, p_scalar);
	return *this;
}

Maths::FVector4 Maths::FMatrix4::operator*(const FVector4& p_vector) const
{
	return Multiply(*this, p_vector);
}

Maths::FMatrix4 Maths::FMatrix4::operator*(const FMatrix4& p_other) const
{
	return Multiply(*this, p_other);
}

Maths::FMatrix4& Maths::FMatrix4::operator*=(const FMatrix4& p_other)
{
	*this = Multiply(*this, p_other);
	return *this;
}

Maths::FMatrix4 Maths::FMatrix4::operator/(float p_scalar) const
{
	return Divide(*this, p_scalar);
}

Maths::FMatrix4& Maths::FMatrix4::operator/=(float p_scalar)
{
	*this = Divide(*this, p_scalar);
	return *this;
}

Maths::FMatrix4 Maths::FMatrix4::operator/(const FMatrix4& p_other) const
{
	return Divide(*this, p_other);
}

Maths::FMatrix4& Maths::FMatrix4::operator/=(const FMatrix4& p_other)
{
	*this = Divide(*this, p_other);
	return *this;
}

float& Maths::FMatrix4::operator()(uint8_t p_row, uint8_t p_column)
{
	if (p_row >= 4 || p_column >= 4)
		throw std::out_of_range(
			"Invalid index : " + std::to_string(p_row) + "," + std::to_string(p_column) + " is out of range");
	return data[4 * p_row + p_column];
}
float Maths::FMatrix4::operator()(uint8_t p_row, uint8_t p_column)const
{
	if (p_row >= 4 || p_column >= 4)
		throw std::out_of_range(
			"Invalid index : " + std::to_string(p_row) + "," + std::to_string(p_column) + " is out of range");
	return data[4 * p_row + p_column];
}


Maths::FMatrix4 Maths::FMatrix4::RotateOnAxisY(float p_rotation)const 
{
	return RotateOnAxisY(*this,p_rotation/180*kPI);
	
}

Maths::FMatrix4 Maths::FMatrix4::RotateOnAxisX(float p_rotation)const
{
	return RotateOnAxisX(*this, p_rotation / 180 * kPI);
}

Maths::FMatrix4 Maths::FMatrix4::RotateOnAxisZ(float p_rotation)const
{
	return RotateOnAxisZ(*this, p_rotation / 180 * kPI);
}

bool Maths::FMatrix4::AreEquals(const FMatrix4& p_left, const FMatrix4& p_right)
{
	return memcmp(&p_left, &p_right, 16 * sizeof(float)) == 0;
}

bool Maths::FMatrix4::AreAlmostEquals(const FMatrix4& p_left, const FMatrix4& p_right)
{
	static float eps = 1e-6;
	for (int i = 0; i < 16;i++) {
		if (abs(p_left.data[i] - p_right.data[i]) > eps) {
			return false;
		}
	}
	return true;
}

Maths::FMatrix4 Maths::FMatrix4::Add(const FMatrix4& p_left, float p_scalar)
{
	FMatrix4 result(p_left);
	for (int8_t i = 0; i < 16; i++)
		result.data[i] += p_scalar;
	return result;
}

Maths::FMatrix4 Maths::FMatrix4::Add(const FMatrix4& p_left, const FMatrix4& p_right)
{
	FMatrix4 result(p_left);
	for (int8_t i = 0; i < 16; i++)
		result.data[i] += p_right.data[i];
	return result;
}

Maths::FMatrix4 Maths::FMatrix4::Subtract(const FMatrix4& p_left, float p_scalar)
{
	FMatrix4 result(p_left);
	for (int8_t i = 0; i < 16; ++i)
		result.data[i] -= p_scalar;
	return result;
}

Maths::FMatrix4 Maths::FMatrix4::Subtract(const FMatrix4& p_left, const FMatrix4& p_right)
{
	FMatrix4 result(p_left);
	for (int8_t i = 0; i < 16; ++i)
		result.data[i] -= p_right.data[i];
	return result;
}

Maths::FMatrix4 Maths::FMatrix4::Multiply(const FMatrix4& p_left, float p_scalar)
{
	FMatrix4 result(p_left);
	for (int8_t i = 0; i < 16; ++i)
		result.data[i] *= p_scalar;
	return result;
}

Maths::FVector4 Maths::FMatrix4::Multiply(const FMatrix4& p_matrix, const FVector4& p_vector)
{
	FVector4 multiply;

	multiply.x = ((p_matrix.data[0] * p_vector.x) + (p_matrix.data[1] * p_vector.y) + (p_matrix.data[2]
		* p_vector.z) + (p_matrix.data[3] * p_vector.w));
	multiply.y = ((p_matrix.data[4] * p_vector.x) + (p_matrix.data[5] * p_vector.y) + (p_matrix.data[6]
		* p_vector.z) + (p_matrix.data[7] * p_vector.w));
	multiply.z = ((p_matrix.data[8] * p_vector.x) + (p_matrix.data[9] * p_vector.y) + (p_matrix.data[10]
		* p_vector.z) + (p_matrix.data[11] * p_vector.w));
	multiply.w = ((p_matrix.data[12] * p_vector.x) + (p_matrix.data[13] * p_vector.y) + (p_matrix.data[
		14] * p_vector.z) + (p_matrix.data[15] * p_vector.w));
	return multiply;
}

Maths::FMatrix4 Maths::FMatrix4::Multiply(const FMatrix4& p_left, const FMatrix4& p_right)
{
	return FMatrix4(
		((p_left.data[0] * p_right.data[0]) + (p_left.data[1] * p_right.data[4]) + (p_left.data[
			2] * p_right.data[8]) + (p_left.data[3] * p_right.data[12])),
		((p_left.data[0] * p_right.data[1]) + (p_left.data[1] * p_right.data[5]) + (p_left.data[
			2] * p_right.data[9]) + (p_left.data[3] * p_right.data[13])),
		((p_left.data[0] * p_right.data[2]) + (p_left.data[1] * p_right.data[6]) + (p_left.data[
			2] * p_right.data[10]) + (p_left.data[3] * p_right.data[14])),
		((p_left.data[0] * p_right.data[3]) + (p_left.data[1] * p_right.data[7]) + (p_left.data[
			2] * p_right.data[11]) + (p_left.data[3] * p_right.data[15])),

		((p_left.data[4] * p_right.data[0]) + (p_left.data[5] * p_right.data[4]) + (p_left.data[
			6] * p_right.data[8]) + (p_left.data[7] * p_right.data[12])),
		((p_left.data[4] * p_right.data[1]) + (p_left.data[5] * p_right.data[5]) + (p_left.data[
			6] * p_right.data[9]) + (p_left.data[7] * p_right.data[13])),
		((p_left.data[4] * p_right.data[2]) + (p_left.data[5] * p_right.data[6]) + (p_left.data[
			6] * p_right.data[10]) + (p_left.data[7] * p_right.data[14])),
		((p_left.data[4] * p_right.data[3]) + (p_left.data[5] * p_right.data[7]) + (p_left.data[
			6] * p_right.data[11]) + (p_left.data[7] * p_right.data[15])),

		((p_left.data[8] * p_right.data[0]) + (p_left.data[9] * p_right.data[4]) + (p_left.data[
			10] * p_right.data[8]) + (p_left.data[11] * p_right.data[12])),
		((p_left.data[8] * p_right.data[1]) + (p_left.data[9] * p_right.data[5]) + (p_left.data[
			10] * p_right.data[9]) + (p_left.data[11] * p_right.data[13])),
		((p_left.data[8] * p_right.data[2]) + (p_left.data[9] * p_right.data[6]) + (p_left.data[
			10] * p_right.data[10]) + (p_left.data[11] * p_right.data[14])),
		((p_left.data[8] * p_right.data[3]) + (p_left.data[9] * p_right.data[7]) + (p_left.data[
			10] * p_right.data[11]) + (p_left.data[11] * p_right.data[15])),

		((p_left.data[12] * p_right.data[0]) + (p_left.data[13] * p_right.data[4]) + (p_left.
			data[14] * p_right.data[8]) + (p_left.data[15] * p_right.data[12])),
		((p_left.data[12] * p_right.data[1]) + (p_left.data[13] * p_right.data[5]) + (p_left.
			data[14] * p_right.data[9]) + (p_left.data[15] * p_right.data[13])),
		((p_left.data[12] * p_right.data[2]) + (p_left.data[13] * p_right.data[6]) + (p_left.
			data[14] * p_right.data[10]) + (p_left.data[15] * p_right.data[14])),
		((p_left.data[12] * p_right.data[3]) + (p_left.data[13] * p_right.data[7]) + (p_left.
			data[14] * p_right.data[11]) + (p_left.data[15] * p_right.data[15])));
}

Maths::FMatrix4 Maths::FMatrix4::Divide(const FMatrix4& p_left, float p_scalar)
{
	FMatrix4 result(p_left);
	for (float& element : result.data)
		element /= p_scalar;

	return result;
}

Maths::FMatrix4 Maths::FMatrix4::Divide(const FMatrix4& p_left, const FMatrix4& p_right)
{
	FMatrix4 leftCopy(p_left);
	return leftCopy * Inverse(p_right);
}

bool Maths::FMatrix4::IsIdentity(const FMatrix4& p_matrix)
{
	return memcmp(Identity.data, p_matrix.data, 16 * sizeof(float)) == 0;
}

float Maths::FMatrix4::GetMinor(float p_minor0, float p_minor1, float p_minor2, float p_minor3, float p_minor4, float p_minor5, float p_minor6, float p_minor7, float p_minor8)
{
	return p_minor0 * (p_minor4 * p_minor8 - p_minor5 * p_minor7)
		- p_minor1 * (p_minor3 * p_minor8 - p_minor5 * p_minor6)
		+ p_minor2 * (p_minor3 * p_minor7 - p_minor4 * p_minor6);
}

float Maths::FMatrix4::Determinant(const FMatrix4& p_matrix)
{
	return p_matrix.data[0] * GetMinor(p_matrix.data[5], p_matrix.data[9], p_matrix.data[13], p_matrix.data[6], p_matrix.data[10], p_matrix.data[14],
		p_matrix.data[7], p_matrix.data[11], p_matrix.data[15])
		- p_matrix.data[4] * GetMinor(p_matrix.data[1], p_matrix.data[9], p_matrix.data[13], p_matrix.data[2], p_matrix.data[10], p_matrix.data[14],
			p_matrix.data[3], p_matrix.data[11], p_matrix.data[15])
		+ p_matrix.data[8] * GetMinor(p_matrix.data[1], p_matrix.data[5], p_matrix.data[13], p_matrix.data[2], p_matrix.data[6], p_matrix.data[14],
			p_matrix.data[3], p_matrix.data[7], p_matrix.data[15])
		- p_matrix.data[12] * GetMinor(p_matrix.data[1], p_matrix.data[5], p_matrix.data[9], p_matrix.data[2], p_matrix.data[6], p_matrix.data[10],
			p_matrix.data[3], p_matrix.data[7], p_matrix.data[11]);
}

Maths::FMatrix4 Maths::FMatrix4::Transpose(const FMatrix4& p_matrix)
{
	FMatrix4 TransposedMatrix(p_matrix);

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			TransposedMatrix.data[4 * j + i] = p_matrix.data[4 * i + j];
		}
	}
	return TransposedMatrix;
}

Maths::FMatrix4 Maths::FMatrix4::TransposeMartix()
{
	return Transpose(*this);;
}

Maths::FMatrix4 Maths::FMatrix4::Inverse(const FMatrix4& p_matrix)
{
	const float determinant = Determinant(p_matrix);
	if (determinant == 0)
		throw std::logic_error("Division by 0");

	const float cof0 = GetMinor(p_matrix.data[5], p_matrix.data[9], p_matrix.data[13], p_matrix.data[6], p_matrix.data[10], p_matrix.data[14],
		p_matrix.data[7], p_matrix.data[11], p_matrix.data[15]);
	const float cof1 = GetMinor(p_matrix.data[1], p_matrix.data[9], p_matrix.data[13], p_matrix.data[2], p_matrix.data[10], p_matrix.data[14],
		p_matrix.data[3], p_matrix.data[11], p_matrix.data[15]);
	const float cof2 = GetMinor(p_matrix.data[1], p_matrix.data[5], p_matrix.data[13], p_matrix.data[2], p_matrix.data[6], p_matrix.data[14],
		p_matrix.data[3], p_matrix.data[7], p_matrix.data[15]);
	const float cof3 = GetMinor(p_matrix.data[1], p_matrix.data[5], p_matrix.data[9], p_matrix.data[2], p_matrix.data[6], p_matrix.data[10],
		p_matrix.data[3], p_matrix.data[7], p_matrix.data[11]);

	const float det = p_matrix.data[0] * cof0 - p_matrix.data[4] * cof1 + p_matrix.data[8] * cof2 - p_matrix.data[12] * cof3;
	if (fabs(det) <= kEpsilon)
		return Identity;
	const float cof4 = GetMinor(p_matrix.data[4], p_matrix.data[8], p_matrix.data[12], p_matrix.data[6], p_matrix.data[10], p_matrix.data[14],
		p_matrix.data[7], p_matrix.data[11], p_matrix.data[15]);
	const float cof5 = GetMinor(p_matrix.data[0], p_matrix.data[8], p_matrix.data[12], p_matrix.data[2], p_matrix.data[10], p_matrix.data[14],
		p_matrix.data[3], p_matrix.data[11], p_matrix.data[15]);
	const float cof6 = GetMinor(p_matrix.data[0], p_matrix.data[4], p_matrix.data[12], p_matrix.data[2], p_matrix.data[6], p_matrix.data[14],
		p_matrix.data[3], p_matrix.data[7], p_matrix.data[15]);
	const float cof7 = GetMinor(p_matrix.data[0], p_matrix.data[4], p_matrix.data[8], p_matrix.data[2], p_matrix.data[6], p_matrix.data[10],
		p_matrix.data[3], p_matrix.data[7], p_matrix.data[11]);

	const float cof8 = GetMinor(p_matrix.data[4], p_matrix.data[8], p_matrix.data[12], p_matrix.data[5], p_matrix.data[9], p_matrix.data[13],
		p_matrix.data[7], p_matrix.data[11], p_matrix.data[15]);
	const float cof9 = GetMinor(p_matrix.data[0], p_matrix.data[8], p_matrix.data[12], p_matrix.data[1], p_matrix.data[9], p_matrix.data[13],
		p_matrix.data[3], p_matrix.data[11], p_matrix.data[15]);
	const float cof10 = GetMinor(p_matrix.data[0], p_matrix.data[4], p_matrix.data[12], p_matrix.data[1], p_matrix.data[5], p_matrix.data[13],
		p_matrix.data[3], p_matrix.data[7], p_matrix.data[15]);
	const float cof11 = GetMinor(p_matrix.data[0], p_matrix.data[4], p_matrix.data[8], p_matrix.data[1], p_matrix.data[5], p_matrix.data[9],
		p_matrix.data[3], p_matrix.data[7], p_matrix.data[11]);

	const float cof12 = GetMinor(p_matrix.data[4], p_matrix.data[8], p_matrix.data[12], p_matrix.data[5], p_matrix.data[9], p_matrix.data[13],
		p_matrix.data[6], p_matrix.data[10], p_matrix.data[14]);
	const float cof13 = GetMinor(p_matrix.data[0], p_matrix.data[8], p_matrix.data[12], p_matrix.data[1], p_matrix.data[9], p_matrix.data[13],
		p_matrix.data[2], p_matrix.data[10], p_matrix.data[14]);
	const float cof14 = GetMinor(p_matrix.data[0], p_matrix.data[4], p_matrix.data[12], p_matrix.data[1], p_matrix.data[5], p_matrix.data[13],
		p_matrix.data[2], p_matrix.data[6], p_matrix.data[14]);
	const float cof15 = GetMinor(p_matrix.data[0], p_matrix.data[4], p_matrix.data[8], p_matrix.data[1], p_matrix.data[5], p_matrix.data[9],
		p_matrix.data[2], p_matrix.data[6], p_matrix.data[10]);

	const float detInv = 1.0f / det;
	FMatrix4 inverse;

	inverse.data[0] = detInv * cof0;
	inverse.data[4] = -detInv * cof4;
	inverse.data[8] = detInv * cof8;
	inverse.data[12] = -detInv * cof12;
	inverse.data[1] = -detInv * cof1;
	inverse.data[5] = detInv * cof5;
	inverse.data[9] = -detInv * cof9;
	inverse.data[13] = detInv * cof13;
	inverse.data[2] = detInv * cof2;
	inverse.data[6] = -detInv * cof6;
	inverse.data[10] = detInv * cof10;
	inverse.data[14] = -detInv * cof14;
	inverse.data[3] = -detInv * cof3;
	inverse.data[7] = detInv * cof7;
	inverse.data[11] = -detInv * cof11;
	inverse.data[15] = detInv * cof15;

	return inverse;
}
Maths::FMatrix4 Maths::FMatrix4::Inverse()
{
	return Inverse(*this);
}

Maths::FMatrix4 Maths::FMatrix4::Translation(const FVector3& p_translation)
{
	return FMatrix4(1, 0, 0, p_translation.x,
		0, 1, 0, p_translation.y,
		0, 0, 1, p_translation.z,
		0, 0, 0, 1);
}

Maths::FMatrix4 Maths::FMatrix4::Translate(const FMatrix4& p_matrix, const FVector3& p_translation)
{
	return  Translation(p_translation)*p_matrix ;
}
Maths::FMatrix4 Maths::FMatrix4::MirrorPlane(float x, float y, float z, float w) {
	Maths::FMatrix4 mirrorMat;
	float a = x;
	float b = y;
	float c = z;
	float d = w;

	float len = sqrtf(a * a + b * b + c * c);
	if (len < 1e-9f) {
		return mirrorMat;
	}

	float nx = a / len;
	float ny = b / len;
	float nz = c / len;
	float pd = d / len;
	

	mirrorMat.data[0] = 1 - 2 * nx * nx;
	mirrorMat.data[1] = -2 * nx * ny;
	mirrorMat.data[2] = -2 * nx * nz;
	mirrorMat.data[3] = -2 * nx * pd;

	mirrorMat.data[4] = -2 * ny * nx;
	mirrorMat.data[5] = 1 - 2 * ny * ny;
	mirrorMat.data[6] = -2 * ny * nz;
	mirrorMat.data[7] = -2 * ny * pd;

	mirrorMat.data[8] = -2 * nz * nx;
	mirrorMat.data[9] = -2 * nz * ny;
	mirrorMat.data[10] = 1 - 2 * nz * nz;
	mirrorMat.data[11] = -2 * nz * pd;

	mirrorMat.data[12] = 0.0f;
	mirrorMat.data[13] = 0.0f;
	mirrorMat.data[14] = 0.0f;
	mirrorMat.data[15] = 1.0f;
	return mirrorMat;
}
Maths::FMatrix4 Maths::FMatrix4::ComputeTransformFromYAxisAndOrigin(const FVector3& yAxis, const FVector3& origin, const FVector3& scale)
{
	FVector3 Y = yAxis;
	Y.Normalize();

	// 构建 X 轴和 Z 轴（正交基）
	FVector3 X, Z;

	// 选择一个不与 Y 平行的参考向量来构建 X 轴
	if (abs(Y.x) < 0.999f)
	{
		X = FVector3::Cross(Y, FVector3(1.0f, 0.0f, 0.0f));
	}
	else
	{
		X = FVector3::Cross(Y, FVector3(0.0f, 0.0f, 1.0f));
	}
	X.Normalize();

	// Z = X × Y
	Z = FVector3::Cross(X, Y);
	Z.Normalize();

	// 重新计算 X 确保正交（Y × Z）
	X = FVector3::Cross(Y, Z);
	X.Normalize();

	// 构建变换矩阵（局部到世界，包含缩放）
	FMatrix4 mat;
	mat.data[0] = X.x * scale.x;   mat.data[1] = X.y * scale.x;   mat.data[2] = X.z * scale.x;   mat.data[3] = 0.0f;
	mat.data[4] = Y.x * scale.y;   mat.data[5] = Y.y * scale.y;   mat.data[6] = Y.z * scale.y;   mat.data[7] = 0.0f;
	mat.data[8] = Z.x * scale.z;   mat.data[9] = Z.y * scale.z;   mat.data[10] = Z.z * scale.z;  mat.data[11] = 0.0f;
	mat.data[12] = origin.x;       mat.data[13] = origin.y;       mat.data[14] = origin.z;       mat.data[15] = 1.0f;

	return mat;
}
Maths::FVector3 Maths::FMatrix4::MulPoint(const FMatrix4& p_matrix, const FVector3& p)
{
	FVector4 hp = { p.x,p.y,p.z,1.0f };
	hp = p_matrix * hp;
	return { hp.x / hp.w,hp.y / hp.w,hp.z / hp.w };
}
Maths::FVector3 Maths::FMatrix4::MulDir(const FMatrix4& p_matrix, const FVector3& p)
{  
	auto dir=FMatrix4::Multiply(p_matrix, FVector4(p, 0.0));
	return FVector3(dir.x,dir.y,dir.z).Normalize();
}
Maths::FVector3 Maths::FMatrix4::MulPoint( const FVector3& p)const 
{
	return MulPoint(*this,p);
}
Maths::FVector3 Maths::FMatrix4::MulDir( const FVector3& p)const 
{
	return MulDir(*this,p);
}
Maths::FVector3 Maths::FMatrix4::MulDirNotNormlaize(const FVector3& p) const
{
	auto dir = FMatrix4::Multiply(*this, FVector4(p, 0.0));
	return FVector3(dir.x, dir.y, dir.z);
}
Maths::FMatrix4 Maths::FMatrix4::RotationOnAxisX(float p_rotation)
{
	return FMatrix4(1, 0, 0, 0,
		0, std::cos(p_rotation), -std::sin(p_rotation), 0,
		0, std::sin(p_rotation), std::cos(p_rotation), 0,
		0, 0, 0, 1);
}

Maths::FMatrix4 Maths::FMatrix4::RotateOnAxisX(const FMatrix4& p_matrix, float p_rotation)
{
	return p_matrix * RotationOnAxisX(p_rotation);
}

Maths::FMatrix4 Maths::FMatrix4::RotationOnAxisY(float p_rotation)
{
	return FMatrix4(std::cos(p_rotation), 0, -std::sin(p_rotation), 0,
		0, 1, 0, 0,
		std::sin(p_rotation), 0, std::cos(p_rotation), 0,
		0, 0, 0, 1);
}

Maths::FMatrix4 Maths::FMatrix4::RotateOnAxisY(const FMatrix4& p_matrix, float p_rotation)
{
	return p_matrix * RotationOnAxisY(p_rotation);
}

Maths::FMatrix4 Maths::FMatrix4::RotationOnAxisZ(float p_rotation)
{
	return FMatrix4(std::cos(p_rotation), -std::sin(p_rotation), 0, 0,
		std::sin(p_rotation), std::cos(p_rotation), 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1);
}

Maths::FMatrix4 Maths::FMatrix4::RotateOnAxisZ(const FMatrix4& p_matrix, float p_rotation)
{
	return p_matrix * RotationOnAxisZ(p_rotation);
}

Maths::FMatrix4 Maths::FMatrix4::RotationYXZ(float p_rotation)
{
	FMatrix4 Xrot = RotationOnAxisX(p_rotation);
	FMatrix4 Yrot = RotationOnAxisY(p_rotation);
	FMatrix4 Zrot = RotationOnAxisZ(p_rotation);

	return Yrot * Xrot * Zrot;
}

Maths::FMatrix4 Maths::FMatrix4::RotateYXZ(const FMatrix4& p_matrix, float p_rotation)
{
	return p_matrix * RotationYXZ(p_rotation);
}

Maths::FMatrix4 Maths::FMatrix4::Scaling(const FVector3& p_scale)
{
	return FMatrix4(p_scale.x, 0, 0, 0,
		0, p_scale.y, 0, 0,
		0, 0, p_scale.z, 0,
		0, 0, 0, 1);
}

Maths::FMatrix4 Maths::FMatrix4::Scale(const FMatrix4& p_matrix, const FVector3& p_scale)
{
	return Scaling(p_scale) *p_matrix ;
}

Maths::FMatrix4 Maths::FMatrix4::Rotation(const FQuaternion& p_quaternion)
{
	return Maths::FQuaternion::ToMatrix4(p_quaternion);
}

Maths::FMatrix4 Maths::FMatrix4::Rotate(const FMatrix4& p_matrix, const FQuaternion& p_quaternion)
{
	return p_matrix * Rotation(p_quaternion);
}

Maths::FMatrix4 Maths::FMatrix4::CreatePerspective(const float p_f, const float p_aspectRatio, const float p_zNear, const float p_zFar)
{
	const float tangent = tanf(p_f / 2.0f * kPI / 180.0f);
	const float height = p_zNear * tangent;
	const float width = height * p_aspectRatio;

	return CreateFrustum(-width, width, -height, height, p_zNear, p_zFar);
}

Maths::FMatrix4 Maths::FMatrix4::CreateOrthographic(const float p_size, const float p_aspectRatio, const float p_zNear, const float p_zFar)
{
	auto ortho = Maths::FMatrix4::Identity;

	const auto right = p_size * p_aspectRatio;
	const auto left = -right;

	const auto top = p_size;
	const auto bottom = -top;

	ortho(0, 0) = 2.0f / (right - left);
	ortho(1, 1) = 2.0f / (top - bottom);
	ortho(0, 3) = -(right + left) / (right - left);
	ortho(1, 3) = -(top + bottom) / (top - bottom);
	ortho(3, 3) = 1.0f;
	if (std::isfinite(p_zFar)) {
		const float zRange = p_zFar - p_zNear;
		ortho(2, 2) = -2.0f / zRange;
		ortho(2, 3) = -(p_zFar + p_zNear) / zRange;
	}
	else {
		// An infinite far plane, as the limit of the pair above: -2/(far-near) goes to
		// zero and -(far+near)/(far-near) goes to -1. Worth knowing what that means for
		// an *orthographic* projection: the depth that comes out is the same for every
		// distance, so the depth buffer stops telling surfaces apart. It is what an
		// infinite far plane is for a perspective view (where the mapping is hyperbolic
		// and only flattens far away); for an orthographic one it is a projection that
		// cannot sort anything, so a camera that wants both an orthographic view and
		// depth belongs at a large finite far plane instead.
		ortho(2, 2) = 0.0f;
		ortho(2, 3) = -1.0f;
	}

	return ortho;
}

Maths::FMatrix4 Maths::FMatrix4::CreateView(const float p_eyeX, const float p_eyeY, const float p_eyeZ, const float p_lookX, const float p_lookY, const float p_lookZ, const float p_upX, const float p_upY, const float p_upZ)
{
	const Maths::FVector3 eye(p_eyeX, p_eyeY, p_eyeZ);
	const Maths::FVector3 look(p_lookX, p_lookY, p_lookZ);
	const Maths::FVector3 up(p_upX, p_upY, p_upZ);

	const Maths::FVector3 forward(eye - look);
	FVector3::Normalize(forward);

	const Maths::FVector3 upXForward(Maths::FVector3::Cross(up, forward));
	FVector3::Normalize(upXForward);

	const Maths::FVector3 v(Maths::FVector3::Cross(forward, upXForward));

	Maths::FMatrix4 View;

	View.data[0] = upXForward.x;
	View.data[1] = upXForward.y;
	View.data[2] = upXForward.z;
	View.data[3] = -Maths::FVector3::Dot(eye, upXForward);

	View.data[4] = v.x;
	View.data[5] = v.y;
	View.data[6] = v.z;
	View.data[7] = -Maths::FVector3::Dot(eye, v);

	View.data[8] = forward.x;
	View.data[9] = forward.y;
	View.data[10] = forward.z;
	View.data[11] = -Maths::FVector3::Dot(eye, forward);

	return View;
}
Maths::FMatrix4 Maths::FMatrix4::CreateCameraView(const FVector3& eye, const FVector3& look, const FVector3& up)
{
	Maths::FVector3 forward(eye - look);
	forward = FVector3::Normalize(forward);

	Maths::FVector3 upXForward(Maths::FVector3::Cross(up, forward));
	upXForward = FVector3::Normalize(upXForward);

	Maths::FVector3 v(Maths::FVector3::Cross(forward, upXForward));

	Maths::FMatrix4 View;

	View.data[0] = upXForward.x;
	View.data[1] = upXForward.y;
	View.data[2] = upXForward.z;
	View.data[3] = -Maths::FVector3::Dot(eye, upXForward);

	View.data[4] = v.x;
	View.data[5] = v.y;
	View.data[6] = v.z;
	View.data[7] = -Maths::FVector3::Dot(eye, v);

	View.data[8] = forward.x;
	View.data[9] = forward.y;
	View.data[10] = forward.z;
	View.data[11] = -Maths::FVector3::Dot(eye, forward);

	return View;
}
Maths::FMatrix4 Maths::FMatrix4::CreateFrustum(const float p_left, const float p_right, const float p_bottom, const float p_top, const float p_zNear, const float p_zFar)
{
	const float maxView = 2.0f * p_zNear;
	const float width = p_right - p_left;
	const float height = p_top - p_bottom;

	FMatrix4 Frustum;

	Frustum.data[0] = maxView / width;
	Frustum.data[5] = maxView / height;
	Frustum.data[2] = (p_right + p_left) / width;
	Frustum.data[6] = (p_top + p_bottom) / height;
	Frustum.data[14] = -1.0f;
	if (std::isfinite(p_zFar)) {
		const float zRange = p_zFar - p_zNear;
		Frustum.data[10] = (-p_zFar - p_zNear) / zRange;
		Frustum.data[11] = (-maxView * p_zFar) / zRange;
	}
	else {
		// No far plane at all: the limit of the two entries above as far goes to
		// infinity, (-far-near)/(far-near) -> -1 and (-2*near*far)/(far-near) -> -2*near.
		// Filament writes the same two lines for the same reason, in
		// FCamera::setProjection. The depth stays hyperbolic - precise up close, flat far
		// away - so a perspective view loses nothing by having no far plane, and with a
		// reversed-Z float depth buffer the precision is relative to the distance
		// (dZ ~ Z * 2^-24, see docs/DepthPrecision.md) with or without one.
		Frustum.data[10] = -1.0f;
		Frustum.data[11] = -maxView;
	}
	Frustum.data[15] = 0.0f;

	return Frustum;
}

Maths::FVector4 Maths::FMatrix4::GetRow(const FMatrix4& p_matrix, uint8_t p_row)
{
	if (p_row >= 4)
		throw std::out_of_range("Invalid index : " + std::to_string(p_row) + " is out of range");

	return FVector4(p_matrix.data[p_row * 4], p_matrix.data[p_row * 4 + 1], p_matrix.data[p_row * 4 + 2], p_matrix.data[p_row * 4 + 3]);
}

Maths::FVector4 Maths::FMatrix4::GetColumn(const FMatrix4& p_matrix, uint8_t p_column)
{
	if (p_column >= 4)
		throw std::out_of_range("Invalid index : " + std::to_string(p_column) + " is out of range");

	return FVector4(p_matrix.data[p_column], p_matrix.data[p_column + 4], p_matrix.data[p_column + 8], p_matrix.data[p_column + 12]);
}
