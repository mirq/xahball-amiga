/* XAHBall - 2D vector math implementation */
#include "vector.h"

//---------------------------------------------------------------------- FUNCTIONS

tVector initVector(int x, int y) {
	tVector sVec;
	sVec.X = fix16_from_int(x);
	sVec.Y = fix16_from_int(y);
	return sVec;
}

tVector addVectors(tVector vecA, tVector vecB) {
	tVector sVec;
	sVec.X = fix16_add(vecA.X, vecB.X);
	sVec.Y = fix16_add(vecA.Y, vecB.Y);
	return sVec;
}

tVector subVectors(tVector vecA, tVector vecB) {
	tVector sVec;
	sVec.X = fix16_sub(vecA.X, vecB.X);
	sVec.Y = fix16_sub(vecA.Y, vecB.Y);
	return sVec;
}

tVector scaleVector(tVector vecA, fix16_t fwScale) {
	tVector sVec;
	sVec.X = fix16_mul(vecA.X, fwScale);
	sVec.Y = fix16_mul(vecA.Y, fwScale);
	return sVec;
}
