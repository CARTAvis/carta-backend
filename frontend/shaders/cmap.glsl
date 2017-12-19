precision highp float;
uniform vec2 uViewportSize;
varying vec2 vUV;
uniform sampler2D u_texture;

uniform float uMinVal;
uniform float uMaxVal;
uniform vec4 uMinCol;
uniform vec4 uMaxCol;

float unpackRGBA(vec4 rgba)
{
  rgba *= 255.0;
  float v = (rgba.b / 2.0) + (rgba.g * 128.0) + (rgba.r * 32768.0);
  //float s = (mod(float(u.b), 2.0)>0.5 ? -1.0 : 1.0);
  return (1.0 + v * pow(2.0, -23.0)) * pow(2.0, rgba.a - 127.0);
}

void main(void) {

	vec4 deltaCol = uMaxCol - uMinCol;
	float range = uMaxVal-uMinVal;
	float zVal = texture2D(u_texture, vUV).r;

	float alpha = clamp((zVal - uMinVal) / range, 0.0, 1.0);
	//alpha = sqrt(alpha);
	//alpha = floor(alpha*5.0)/5.0;
	gl_FragColor = uMinCol + alpha * deltaCol;
}