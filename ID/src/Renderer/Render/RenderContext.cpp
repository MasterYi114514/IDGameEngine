#include "Renderer/Render/RenderContext.hpp"
#include "Renderer/Mesh/Model.hpp"
#include "Renderer/Mesh/Mesh.hpp"

namespace ID
{
    ModelSE::SubmitEntry(const Model& model, const Mat4& outer_transform, const PipelineID pipeline_id)
        : material(&model.get_material()), mesh(model.get_mesh_id()),
          world_transform(outer_transform * model.get_local_transform()), pipeline(pipeline_id) { }
}