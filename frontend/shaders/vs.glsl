attribute vec3 aVertexPosition;
attribute vec2 aVertexUV;

//uniform vec2 uViewportSize;
varying vec2 vUV;

void main(void) {
	gl_Position =  vec4(aVertexPosition, 1.0);
	vUV = aVertexUV;
}