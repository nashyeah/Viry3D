/*
* Viry3D
* Copyright 2014-2017 by Stack - stackos@qq.com
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

UniformBuffer(1, 0) uniform buf_vs_obj {
	mat4 _World;
} u_buf_obj;

UniformBuffer(0, 2) uniform buf_vs {
	mat4 _ViewProjection;
	vec4 _MainTex_ST;
} u_buf;

layout (location = 0) in vec4 a_pos;
layout (location = 1) in vec2 a_uv;

Varying(0) out vec2 v_uv;

void main() {
	gl_Position = a_pos * u_buf_obj._World * u_buf._ViewProjection;
	v_uv = a_uv * u_buf._MainTex_ST.xy + u_buf._MainTex_ST.zw;

	vulkan_convert();
}
