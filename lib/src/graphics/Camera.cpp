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

#include "Camera.h"
#include "GameObject.h"
#include "Debug.h"
#include "Profiler.h"
#include "Graphics.h"
#include "Material.h"
#include "RenderPass.h"
#include "RenderTexture.h"
#include "time/Time.h"
#include "renderer/Renderer.h"
#include "postprocess/ImageEffect.h"

namespace Viry3D
{
	DEFINE_COM_CLASS(Camera);

	List<Camera*> Camera::m_cameras;
	Camera* Camera::m_current;
	Ref<FrameBuffer> Camera::m_post_target_front;
	Ref<FrameBuffer> Camera::m_post_target_back;

	void Camera::Init()
	{
	}

	void Camera::Deinit()
	{
		m_cameras.Clear();
		m_post_target_front.reset();
		m_post_target_back.reset();
	}

	bool Camera::IsValidCamera(Camera* cam)
	{
		bool exist = false;
		for (auto i : m_cameras)
		{
			if (i == cam)
			{
				exist = true;
				break;
			}
		}
		return exist;
	}

	Camera::Camera():
		m_culling_mask(-1),
		m_matrix_dirty(true),
        m_matrix_external(false),
        m_frustum_culling(true),
		m_render_mode(CameraRenderMode::Normal)
	{
		m_cameras.AddLast(this);

		this->SetClearFlags(CameraClearFlags::Color);
		this->SetClearColor(Color(0, 0, 0, 1));
		this->SetDepth(0);
		this->SetOrthographic(false);
		this->SetOrthographicSize(1);
		this->SetFieldOfView(45);
		this->SetClipNear(0.3f);
		this->SetClipFar(1000);
		this->SetRect(Rect(0, 0, 1, 1));
		this->SetHdr(false);
	}

	Camera::~Camera()
	{
		m_cameras.Remove(this);
	}

	void Camera::SetCullingMask(int mask)
	{
		if (m_culling_mask != mask)
		{
			m_culling_mask = mask;

			Renderer::SetCullingDirty(this);
		}
	}

	void Camera::SetDepth(int depth)
	{
		m_depth = depth;

		m_cameras.Sort(Less);
	}

	bool Camera::CanRender() const
	{
		return this->GetGameObject()->IsActiveInHierarchy() && this->IsEnable();
	}

	bool Camera::IsCulling(const Ref<GameObject>& obj) const
	{
		return (this->GetCullingMask() & (1 << obj->GetLayer())) == 0;
	}

	bool Camera::Less(const Camera *c1, const Camera *c2)
	{
		return c1->GetDepth() < c2->GetDepth();
	}

	void Camera::DeepCopy(const Ref<Object>& source)
	{
		assert(!"can not copy a camera");
	}

	void Camera::OnTranformChanged()
	{
		m_matrix_dirty = true;

		Renderer::SetCullingDirty(this);
	}

	void Camera::OnResize(int width, int height)
	{
		for (auto i : m_cameras)
		{
			i->m_render_pass.reset();
			i->m_render_pass_post.reset();
			i->m_matrix_dirty = true;

			Renderer::SetCullingDirty(i);
		}
		m_post_target_front.reset();
		m_post_target_back.reset();

		Renderer::OnResize(width, height);
	}

	void Camera::OnPause()
	{
		for (auto i : m_cameras)
		{
			i->m_render_pass.reset();
			i->m_render_pass_post.reset();
			i->m_matrix_dirty = true;

			Renderer::SetCullingDirty(i);
		}
		m_post_target_front.reset();
		m_post_target_back.reset();

		Renderer::OnPause();
	}

	void Camera::RenderAll()
	{
		Profiler::SampleBegin("Camera::RenderAll");

		for (auto i : m_cameras)
		{
			if (i->CanRender())
			{
				m_current = i;
				i->Prepare();
				i->Render();
			}
		}
		m_current = NULL;

		Profiler::SampleEnd();
	}

	void Camera::Prepare()
	{
		this->DecideTarget();

		if (!m_render_pass)
		{
			if (m_target_rendering)
			{
				m_render_pass = RenderPass::Create(m_target_rendering->color_texture, m_target_rendering->depth_texture, this->GetClearFlags(), true, this->GetRect());
				m_render_pass_post = RenderPass::Create(m_target_rendering->color_texture, m_target_rendering->depth_texture, CameraClearFlags::Nothing, true, this->GetRect());
			}
			else
			{
				m_render_pass = RenderPass::Create(Ref<RenderTexture>(), Ref<RenderTexture>(), this->GetClearFlags(), true, this->GetRect());
				m_render_pass_post = RenderPass::Create(Ref<RenderTexture>(), Ref<RenderTexture>(), CameraClearFlags::Nothing, true, this->GetRect());
			}
		}

		m_render_pass->Bind();

		Renderer::PrepareAllPass();

		m_render_pass->Unbind();
	}

	void Camera::BeginRenderPass(bool post) const
	{
		Graphics::GetDisplay()->WaitQueueIdle();
		if (post)
		{
			m_render_pass_post->Begin(this->GetClearColor());
		}
		else
		{
			m_render_pass->Begin(this->GetClearColor());
		}
	}

	void Camera::EndRenderPass(bool post) const
	{
		if (post)
		{
			m_render_pass_post->End();
			Graphics::GetDisplay()->SubmitQueue(m_render_pass_post->GetCommandBuffer());
		}
		else
		{
			m_render_pass->End();
			Graphics::GetDisplay()->SubmitQueue(m_render_pass->GetCommandBuffer());
		}
	}

	void Camera::Render()
	{
		this->BeginRenderPass(false);
		Renderer::RenderAllPass();
		this->EndRenderPass(false);

		this->GetGameObject()->OnPostRender();
		if (m_post_render_func)
		{
			m_post_render_func();
		}

		this->PostProcess();
	}

	Ref<FrameBuffer> Camera::GetPostTargetFront()
	{
		if (!m_post_target_front)
		{
			m_post_target_front = RefMake<FrameBuffer>();
			m_post_target_front->color_texture = RenderTexture::Create(
				this->GetTargetWidth(),
				this->GetTargetHeight(),
				RenderTextureFormat::RGBA32,
				DepthBuffer::Depth_0,
				FilterMode::Bilinear);
		}

		return m_post_target_front;
	}

	Ref<FrameBuffer> Camera::GetPostTargetBack()
	{
		if (!m_post_target_back)
		{
			m_post_target_back = RefMake<FrameBuffer>();
			m_post_target_back->color_texture = RenderTexture::Create(
				this->GetTargetWidth(),
				this->GetTargetHeight(),
				RenderTextureFormat::RGBA32,
				DepthBuffer::Depth_0,
				FilterMode::Bilinear);
		}

		return m_post_target_back;
	}

	void Camera::SwapPostTargets()
	{
		if (m_post_target_front && m_post_target_back)
		{
			RefSwap(m_post_target_front, m_post_target_back);
		}
	}

	void Camera::DecideTarget()
	{
		auto effects = this->GetGameObject()->GetComponents<ImageEffect>();

		if (effects.Empty())
		{
			m_target_rendering = m_frame_buffer;
		}
		else
		{
			m_target_rendering = this->GetPostTargetFront();
		}
	}

	void Camera::PostProcess()
	{
		auto effects = this->GetGameObject()->GetComponents<ImageEffect>();

		if (!effects.Empty())
		{
			for (int i = 0; i < effects.Size(); i++)
			{
				Ref<FrameBuffer> src = this->GetPostTargetFront();
				Ref<FrameBuffer> dest;
				Ref<RenderTexture> src_texture;
				Ref<RenderTexture> dest_texture;

				if (i == effects.Size() - 1)
				{
					dest = m_frame_buffer;
				}
				else
				{
					dest = this->GetPostTargetBack();
				}

				if (src)
				{
					src_texture = src->color_texture;
				}
				if (dest)
				{
					dest_texture = dest->color_texture;
				}

				effects[i]->OnRenderImage(src_texture, dest_texture);

				this->SwapPostTargets();
			}
		}
	}

	int Camera::GetTargetWidth() const
	{
		int width;

		if (m_frame_buffer)
		{
			width = m_frame_buffer->color_texture ? m_frame_buffer->color_texture->GetWidth() : m_frame_buffer->depth_texture->GetWidth();
		}
		else
		{
			width = Graphics::GetDisplay()->GetWidth();
		}

		return width;
	}

	int Camera::GetTargetHeight() const
	{
		int height;

		if (m_frame_buffer)
		{
			height = m_frame_buffer->color_texture ? m_frame_buffer->color_texture->GetHeight() : m_frame_buffer->depth_texture->GetHeight();
		}
		else
		{
			height = Graphics::GetDisplay()->GetHeight();
		}

		return height;
	}

	void Camera::UpdateMatrix()
	{
		m_matrix_dirty = false;

		int width = this->GetTargetWidth();
		int height = this->GetTargetHeight();

		auto transform = GetTransform();

		m_view_matrix = Matrix4x4::LookTo(
			transform->GetPosition(),
			transform->GetForward(),
			transform->GetUp());

		if (!this->IsOrthographic())
		{
			m_projection_matrix = Matrix4x4::Perspective(this->GetFieldOfView(), width / (float) height, this->GetClipNear(), this->GetClipFar());
		}
		else
		{
			float ortho_size = this->GetOrthographicSize();
			auto rect = this->GetRect();

			float top = ortho_size;
			float bottom = -ortho_size;
			float plane_h = ortho_size * 2;
			float plane_w = plane_h * (width * rect.width) / (height * rect.height);
			m_projection_matrix = Matrix4x4::Ortho(-plane_w / 2, plane_w / 2, bottom, top, this->GetClipNear(), this->GetClipFar());
		}

		m_view_projection_matrix = m_projection_matrix * m_view_matrix;
	}

	const Matrix4x4& Camera::GetViewMatrix()
	{
        if (m_matrix_external)
        {
            return m_view_matrix_external;
        }
        
		if (m_matrix_dirty)
		{
			UpdateMatrix();
		}

		return m_view_matrix;
	}

	const Matrix4x4& Camera::GetProjectionMatrix()
	{
        if (m_matrix_external)
        {
            return m_projection_matrix_external;
        }
        
		if (m_matrix_dirty)
		{
			UpdateMatrix();
		}

		return m_projection_matrix;
	}
    
	const Frustum& Camera::GetFrustum()
	{
		if (m_matrix_dirty)
		{
			UpdateMatrix();

			m_frustum = Frustum(m_view_projection_matrix);
		}

		return m_frustum;
	}

	void Camera::SetFrameBuffer(const Ref<FrameBuffer>& frame_buffer)
	{
		m_matrix_dirty = true;
		m_frame_buffer = frame_buffer;
	}

	Vector3 Camera::ScreenToViewportPoint(const Vector3& position)
	{
		float x = (position.x / GetTargetWidth() - m_rect.x) / m_rect.width;
		float y = (position.y / GetTargetHeight() - m_rect.y) / m_rect.height;
		return Vector3(x, y, position.z);
	}

	Vector3 Camera::ViewportToScreenPoint(const Vector3& position)
	{
		float x = (position.x * m_rect.width + m_rect.x) * GetTargetWidth();
		float y = (position.y * m_rect.height + m_rect.y) * GetTargetHeight();
		return Vector3(x, y, position.z);
	}

	Vector3 Camera::ScreenToWorldPoint(const Vector3& position)
	{
		Vector3 pos_viewport = this->ScreenToViewportPoint(position);
		Vector3 pos_proj = pos_viewport * 2.0f - Vector3(1.0f, 1.0f, 0);
		Matrix4x4 vp_inverse = (this->GetProjectionMatrix() * this->GetViewMatrix()).Inverse();

		if (IsOrthographic())
		{
			Vector4 pos_world = vp_inverse * Vector4(pos_proj.x, pos_proj.y, 0, 1.0f);
			pos_world *= 1.0f / pos_world.w;

			Vector3 origin = Vector3(pos_world.x, pos_world.y, pos_world.z);
			Vector3 direction = this->GetTransform()->GetForward();

			Ray ray_screen(origin, direction);
			float ray_len = position.z - this->GetClipNear();

			return ray_screen.GetPoint(ray_len);
		}
		else
		{
			Vector4 pos_world = vp_inverse * Vector4(pos_proj.x, pos_proj.y, -1.0f, 1.0f);
			pos_world *= 1.0f / pos_world.w;

			Vector3 origin = this->GetTransform()->GetPosition();
			Vector3 direction = Vector3(pos_world.x, pos_world.y, pos_world.z) - origin;

			Ray ray_screen(origin, direction);
			Ray ray_forward(origin, this->GetTransform()->GetForward());
			Vector3 plane_point = ray_forward.GetPoint(position.z);
			float ray_len;
			bool hit = Mathf::RayPlaneIntersection(ray_screen, ray_forward.GetDirection(), plane_point, ray_len);
			assert(hit);

			return ray_screen.GetPoint(ray_len);
		}
	}

	Vector3 Camera::WorldToScreenPoint(const Vector3& position)
	{
		Vector3 pos_view = this->GetViewMatrix().MultiplyPoint3x4(position);
		Vector3 pos_proj = this->GetProjectionMatrix().MultiplyPoint3x4(pos_view);
		Vector3 pos_viewport = (pos_proj + Vector3(1.0f, 1.0f, 0)) * 0.5f;
		pos_viewport.z = pos_view.z;
		return this->ViewportToScreenPoint(pos_viewport);
	}

	Ray Camera::ScreenPointToRay(const Vector3& position)
	{
		Vector3 pos_world = this->ScreenToWorldPoint(Vector3(position.x, position.y, this->GetClipNear()));

		Vector3 origin = pos_world;
		Vector3 direction;

		if (IsOrthographic())
		{
			direction = this->GetTransform()->GetForward();
		}
		else
		{
			direction = origin - this->GetTransform()->GetPosition();
		}

		return Ray(origin, direction);
	}
}
