// MeshPipelineRecord.cpp - MeshPipeline の毎フレーム描画コマンド記録
// (RecordInstancedMultiPart/RecordLateOpaqueInstancedMultiPart/
// RecordTransparentInstancedMultiPart/
// RecordHighlightInstanced/RecordHighlightSelf)。
// パイプライン/PSO生成(MeshPipelinePSO.cpp)、初期化(MeshPipeline.cpp)から分離。
#include "MeshPipeline.h"
#include "SubmeshAlphaClassify.h"
#include "../resource/RenderTuning.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <spdlog/spdlog.h>

namespace rs::render {

void MeshPipeline::RecordHighlightInstanced(ID3D12GraphicsCommandList* cmdList,
                                            const MeshConstants& baseConstants,
                                            UINT frameIndex, const float tintColor[4]) {
    if (geometry_.IndexCount() == 0) return;
    UINT f = frameIndex % kFrames;

    shared_->BindRootSignature(rs::rhi::CommandList{cmdList}, baseConstants, {srvHeap_, srvSize_});
    cmdList->SetPipelineState(shared_->InstancedHighlight());
    dynamicBuffers_.BindInstanceMatOverrideTable(cmdList, f);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &geometry_.VertexBufferView());
    cmdList->IASetIndexBuffer(&geometry_.IndexBufferView());

    {
        D3D12_GPU_DESCRIPTOR_HANDLE partWorldHandle =
            srvHeap_->GetGPUDescriptorHandleForHeapStart();
        partWorldHandle.ptr += SIZE_T(dynamicBuffers_.FirstPartWorldIndex() + f) * srvSize_;
        cmdList->SetGraphicsRootDescriptorTable(2, partWorldHandle);
    }

    // materialIndex はわざと範囲外(materialSet_.Count())を渡し、WriteMeshConstantsに
    // materialSet_由来のdiffuse上書きをさせない(tintedのdiffuseがそのまま使われる)。
    // PSMainHighlightはテクスチャ/UV変換を一切参照しないため、useTexture等の
    // 既定値で問題ない。
    MeshConstants tinted = baseConstants;
    tinted.diffuse[0] = tintColor[0];
    tinted.diffuse[1] = tintColor[1];
    tinted.diffuse[2] = tintColor[2];
    tinted.diffuse[3] = tintColor[3];
    const UINT cbSlot = 0;
    const D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        WriteMeshConstants(tinted, f, cbSlot, static_cast<uint32_t>(materialSet_.Count()), false);
    cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

    // 不透明+cutout+lateOpaque全submeshを1インスタンスで塗る(半透明submeshは
    // 対象外)。
    for (const SubMesh& sm : geometry_.Submeshes()) {
        if (sm.IsTransparent()) continue;
        cmdList->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
    }
}

MeshConstants MeshPipeline::BindInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                                   const MeshConstants& baseConstants, UINT f,
                                                   UINT partsPerInstance) {
    shared_->BindRootSignature(rs::rhi::CommandList{cmdList}, baseConstants, {srvHeap_, srvSize_});
    dynamicBuffers_.BindInstanceMatOverrideTable(cmdList, f);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &geometry_.VertexBufferView());
    cmdList->IASetIndexBuffer(&geometry_.IndexBufferView());
    D3D12_GPU_DESCRIPTOR_HANDLE partWorldHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    partWorldHandle.ptr += SIZE_T(dynamicBuffers_.FirstPartWorldIndex() + f) * srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(2, partWorldHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE overrideHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    overrideHandle.ptr += SIZE_T(dynamicBuffers_.FirstInstanceMatOverrideIndex() + f * 2) * srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(3, overrideHandle);
    MeshConstants base = baseConstants;
    base.partsPerInstance = partsPerInstance;
    base.useInstanceMatOverride = dynamicBuffers_.HasInstanceMatOverrides() ? 1u : 0u;
    return base;
}

void MeshPipeline::DrawInstancedSubmesh(ID3D12GraphicsCommandList* cmdList, MeshConstants& base,
                                        UINT f, const SubMesh& submesh, UINT instanceCount,
                                        UINT& cbIndex, MeshConstantBuffer& constantBuffer) {
    const UINT cbLimit = constantBuffer.Capacity(f) > 1 ? constantBuffer.Capacity(f) - 2 : 0;
    // 判定は「描画対象のsubmeshに割り当てるスロットが尽きた」ときだけ行う
    // (パスのフィルタに合致しないsubmeshで先に評価すると、全submeshを描き切って
    // cbIndex==cbLimitになった後の後続パスで誤検知する)。
    if (cbIndex >= cbLimit) {
        if (!cbOverflowWarned_) {
            spdlog::warn("[mesh] RecordInstancedMultiPart cb overflow: geometryKey={} submeshes={} "
                         "recordedDraws={} cbCapacity={}; some draws skipped",
                         geometryKey_, geometry_.Submeshes().size(), cbIndex,
                         constantBuffer.Capacity(f));
            cbOverflowWarned_ = true;
        }
        return;
    }
    base.materialIndex = submesh.materialIndex;
    // 他の物の後に重ね描きする層(窓ガラス Blended・後描き不透明 LateOpaque・下地に重ねる BlendedInPlace)は、
    // 深度プリパスに載らない。画面のAO(アンビエントオクルージョン)を引くと、背後の面の暗がりがその層に出てしまうので、
    // この submesh だけAOを使わない。
    const std::uint32_t screenAoIndex = base.adaptation[2];
    if (submesh.IsBlended() || submesh.IsBlendedInPlace() || submesh.IsLateOpaque()) base.adaptation[2] = 0;
    cmdList->SetGraphicsRootConstantBufferView(
        0, WriteMeshConstantsTo(constantBuffer, base, f, cbIndex++, submesh.materialIndex, submesh.alphaTestClip,
                                submesh.alphaSplit));
    base.adaptation[2] = screenAoIndex;
    cmdList->DrawIndexedInstanced(submesh.indexCount, instanceCount, submesh.indexOffset, 0, 0);
}

void MeshPipeline::RecordInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                            const MeshConstants& baseConstants, UINT frameIndex,
                                            UINT instanceCount, UINT partsPerInstance) {
    const UINT f = frameIndex % kFrames;
    lateOpaqueFrame_[f] = kNoLateOpaqueFrame;
    if (geometry_.IndexCount() == 0 || instanceCount == 0 || partsPerInstance == 0) return;
    MeshConstants base = BindInstancedMultiPart(cmdList, baseConstants, f, partsPerInstance);
    UINT cbIndex = 0;
    UINT blendedCount = 0;
    auto draw = [&](const SubMesh& submesh) {
        DrawInstancedSubmesh(cmdList, base, f, submesh, instanceCount, cbIndex, constantBuffer_);
    };
    // 不透明のうち、テクスチャに中間alphaを持つsubmesh(車体の陰影デカール、
    // ヘッドライトのレンズ等)はcutoutと同じ2段描画にする。1パス・深度書込ALLの
    // ままだと、色をほとんど載せない中間alpha領域まで深度オクルーダーになり、
    // その奥のジオメトリが後から描かれたときに深度で落ちる。大半の
    // テクスチャがalpha全域で閾値以上のため、この分岐は実質no-opで、効果があるのは
    // 陰影グラデーションを持つ数枚だけ。
    ID3D12PipelineState* solidPso = shared_->InstancedMultiPartSolid();
    ID3D12PipelineState* currentPso = nullptr;
    // 穴のある材質(cutout)も穴のない材質と同じ1本のループで、submesh順に描く。RailSim II は
    // 材質を Diffuse.a の大きい順(同じ値なら材質番号順)に描き、穴の有無で順番を変えない。
    // cutoutだけを後ろへ回すと、穴の奥に先に描いた面(運転台のパネルなど)が、後から描く
    // 手前の面の深度に隠れて、穴の形に切り抜かれて見える。
    // cutout用PSOはRailSim II 互換のためブレンド有効(=描画順依存)。深度専用パスを
    // 全submesh分先にまとめて書き切ってからカラーパスを全submesh分回すと、
    // 後続のカラー描画が「別submeshが先に書いた深度」に負けて抜けたり、
    // 本来奥にあるはずの背景と誤ってブレンドされたりする
    // (たとえば駅の屋根と地面が消える)。1submeshごとに深度専用→カラーの
    // 順で完結させ、次のsubmeshに進む。
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        if (submesh.IsTransparent() || submesh.IsLateOpaque()) continue;
        // Blendedは、全パイプラインの不透明描画の後にRecordBlendedInstancedMultiPartが描く。
        if (submesh.IsBlended()) {
            ++blendedCount;
            continue;
        }
        if (submesh.IsBlendedInPlace()) {
            // 深度専用パスは挟まない(低いalphaのtexelまで切り抜かれて、奥の面が座席の形に見える)。
            // 深度比較はGREATER_EQUALのcutout用PSOで描く。点灯レンズの光のように、下地と同じ面に
            // 重ねる材質が、厳密GREATERの深度テストで落ちないようにする。
            if (currentPso != shared_->InstancedMultiPartCutout()) {
                cmdList->SetPipelineState(shared_->InstancedMultiPartCutout());
                currentPso = shared_->InstancedMultiPartCutout();
            }
            draw(submesh);
            continue;
        }
        if (submesh.IsOpaqueAlphaTex() || submesh.IsCutoutPass()) {
            cmdList->SetPipelineState(shared_->InstancedMultiPartCutoutDepthOnly());
            draw(submesh);
            cmdList->SetPipelineState(shared_->InstancedMultiPartCutout());
            draw(submesh);
            currentPso = shared_->InstancedMultiPartCutout();
            continue;
        }
        if (currentPso != solidPso) {
            cmdList->SetPipelineState(solidPso);
            currentPso = solidPso;
        }
        draw(submesh);
    }
    // BlendedとlateOpaqueは、全パイプラインの不透明描画後に描く。定数バッファはこのパスの
    // 続きのスロットから使わせる(Blendedが1submeshにつき1枠、その後ろからlateOpaque)。
    lateOpaqueFrame_[f] = frameIndex;
    blendedCbStart_[f] = cbIndex;
    lateOpaqueCbStart_[f] = cbIndex + blendedCount;
}

void MeshPipeline::RecordBlendedInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                                   const MeshConstants& baseConstants,
                                                   UINT frameIndex, UINT instanceCount,
                                                   UINT partsPerInstance) {
    const UINT f = frameIndex % kFrames;
    if (lateOpaqueFrame_[f] != frameIndex) return;
    const std::vector<SubMesh>& submeshes = geometry_.Submeshes();
    if (std::none_of(submeshes.begin(), submeshes.end(),
                     [](const SubMesh& submesh) { return submesh.IsBlended(); })) return;
    MeshConstants base = BindInstancedMultiPart(cmdList, baseConstants, f, partsPerInstance);
    UINT cbIndex = blendedCbStart_[f];
    // 材質色Diffuse.a<1の材質(窓ガラスなど)は深度を書く。パイプラインごとの描画の途中で描くと、
    // 後から描く物(窓越しの景色)が深度で落ちるため、全不透明の後にまとめて描く。
    // submesh順(Diffuse.aの降順)は保つので、ブラインドや網は先に、ガラスは後にブレンドされる。
    // 深度専用パスは挟まない(低いalphaのtexelまで切り抜かれて、奥の面が座席の形に見える)。
    // 深度比較はGREATER_EQUALのcutout用PSOで描く。点灯レンズの光のように、下地と同じ面に
    // 重ねる材質が、厳密GREATERの深度テストで落ちないようにする。
    cmdList->SetPipelineState(shared_->InstancedMultiPartCutout());
    for (const SubMesh& submesh : submeshes) {
        if (!submesh.IsBlended()) continue;
        DrawInstancedSubmesh(cmdList, base, f, submesh, instanceCount, cbIndex, constantBuffer_);
    }
}

void MeshPipeline::RecordLateOpaqueInstancedMultiPart(ID3D12GraphicsCommandList* cmdList,
                                                      const MeshConstants& baseConstants,
                                                      UINT frameIndex, UINT instanceCount,
                                                      UINT partsPerInstance) {
    const UINT f = frameIndex % kFrames;
    // 同フレームのRecordInstancedMultiPartを経ていなければ描かない(スロット割当の基点が無い)。
    if (lateOpaqueFrame_[f] != frameIndex) return;
    const std::vector<SubMesh>& submeshes = geometry_.Submeshes();
    if (std::none_of(submeshes.begin(), submeshes.end(),
                     [](const SubMesh& submesh) { return submesh.IsLateOpaque(); })) return;
    MeshConstants base = BindInstancedMultiPart(cmdList, baseConstants, f, partsPerInstance);
    UINT cbIndex = lateOpaqueCbStart_[f];
    auto draw = [&](const SubMesh& submesh) {
        DrawInstancedSubmesh(cmdList, base, f, submesh, instanceCount, cbIndex, constantBuffer_);
    };
    // RailSim II はTransparent指定の材質をフレーム内の全物体の描画後にまとめて描く。モデル単位で最後に回すだけだと、ここで書く深度が
    // 後続パイプラインの奥の物体を落とすため、呼び出し側が全不透明パスの後で呼ぶ。
    // lateOpaqueもcutoutと同じ「深度専用パス→カラーパス」の2段で描く。
    // lateOpaqueには不透明部と抜き部を1枚のテクスチャで兼ねる材質(窓周りの縁など)が
    // 含まれ、1パスで深度書込ALL+低い閾値(0.02)のまま描くと、縁のアンチエイリアス
    // texel(alphaがごく小さい)まで深度オクルーダーになる。そのtexelは色をほとんど
    // 載せないのに後段の半透明パスを深度で落とすため、ガラスが縁の内側で帯状に
    // 欠ける。深度は
    // kCutoutOccluderAlphaThreshold以上のtexelだけが確定させる。
    // 状態はcutout用PSOと同一(GREATER_EQUAL、バイアス無し)なのでそのまま流用する。
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        if (!submesh.IsLateOpaque()) continue;
        cmdList->SetPipelineState(shared_->InstancedMultiPartCutoutDepthOnly());
        draw(submesh);
        cmdList->SetPipelineState(shared_->InstancedMultiPartCutout());
        draw(submesh);
    }
}

void MeshPipeline::RecordTransparentInstancedMultiPart(
    ID3D12GraphicsCommandList* cmdList, const MeshConstants& baseConstants, UINT frameIndex,
    UINT instanceCount, UINT partsPerInstance, bool wboit) {
    if (geometry_.IndexCount() == 0 || instanceCount == 0 || partsPerInstance == 0 || transparency_.Empty()) return;
    const UINT f = frameIndex % kFrames;
    if (!transparency_.FrameReady(f)) return;
    shared_->BindRootSignature(rs::rhi::CommandList{cmdList}, baseConstants, {srvHeap_, srvSize_});
    dynamicBuffers_.BindInstanceMatOverrideTable(cmdList, f);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &geometry_.VertexBufferView());
    D3D12_GPU_DESCRIPTOR_HANDLE partWorldHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    partWorldHandle.ptr += SIZE_T(dynamicBuffers_.FirstPartWorldIndex() + f) * srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(2, partWorldHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE overrideHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    overrideHandle.ptr += SIZE_T(dynamicBuffers_.FirstInstanceMatOverrideIndex() + f * 2) * srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(3, overrideHandle);
    transparency_.SyncMatDataToGpu(f);
    if (wboit && transparency_.ContentDirty(f)) {
        uint32_t* indices = reinterpret_cast<uint32_t*>(transparency_.IndexMapped(f));
        uint32_t* materials = transparency_.TriMatMapped(f);
        for (size_t i = 0; i < transparency_.Tris().size(); ++i) {
            const TransparentTri& triangle = transparency_.Tris()[i];
            indices[i * 3] = triangle.idx[0]; indices[i * 3 + 1] = triangle.idx[1]; indices[i * 3 + 2] = triangle.idx[2]; materials[i] = triangle.materialIndex;
        }
        transparency_.ClearContentDirty(f);
    } else if (!wboit) {
        static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                             0, 0, 1, 0, 0, 0, 0, 1};
        transparency_.SortScratch().clear();
        transparency_.SortScratch().reserve(transparency_.Tris().size());
        for (const TransparentTri& triangle : transparency_.Tris()) {
            const float* world = triangle.partIndex < dynamicBuffers_.PartXforms().size()
                                     ? dynamicBuffers_.PartXforms()[triangle.partIndex].world
                                     : kIdentity;
            const float worldX = triangle.center[0] * world[0] + triangle.center[1] * world[4] +
                                 triangle.center[2] * world[8] + world[12];
            const float worldY = triangle.center[0] * world[1] + triangle.center[1] * world[5] +
                                 triangle.center[2] * world[9] + world[13];
            const float worldZ = triangle.center[0] * world[2] + triangle.center[1] * world[6] +
                                 triangle.center[2] * world[10] + world[14];
            const float dx = worldX - baseConstants.viewCameraPos[0];
            const float dy = worldY - baseConstants.viewCameraPos[1];
            const float dz = worldZ - baseConstants.viewCameraPos[2];
            transparency_.SortScratch().emplace_back(dx * dx + dy * dy + dz * dz, &triangle);
        }
        std::stable_sort(transparency_.SortScratch().begin(), transparency_.SortScratch().end(),
                         [](const auto& left, const auto& right) { return left.first > right.first; });
        uint32_t* indices = reinterpret_cast<uint32_t*>(transparency_.IndexMapped(f));
        uint32_t* materials = transparency_.TriMatMapped(f);
        for (size_t i = 0; i < transparency_.SortScratch().size(); ++i) {
            const TransparentTri* triangle = transparency_.SortScratch()[i].second;
            indices[i * 3] = triangle->idx[0];
            indices[i * 3 + 1] = triangle->idx[1];
            indices[i * 3 + 2] = triangle->idx[2];
            materials[i] = triangle->materialIndex;
        }
        transparency_.ClearContentDirty(f);
    }
    MeshConstants base = baseConstants;
    base.partsPerInstance = partsPerInstance;
    base.useInstanceMatOverride = dynamicBuffers_.HasInstanceMatOverrides() ? 1u : 0u;
    const UINT baseSlot = constantBuffer_.Capacity(f) > 1 ? constantBuffer_.Capacity(f) - 2 : 0;
    cmdList->SetPipelineState(wboit ? shared_->InstancedMultiPartTransparentWboit()
                                    : shared_->Transparent());
    cmdList->IASetIndexBuffer(&transparency_.IbView(f));
    cmdList->SetGraphicsRootConstantBufferView(0, WriteMeshConstants(base, f, baseSlot, 0, false));
    D3D12_GPU_DESCRIPTOR_HANDLE transBuf = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    transBuf.ptr += SIZE_T(transparency_.FirstTransBufIndex() + f * 2) * srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(1, transBuf);
    cmdList->DrawIndexedInstanced(UINT(transparency_.Tris().size() * 3), instanceCount, 0, 0, 0);
}

void MeshPipeline::RecordFadeInstancedMultiPart(
    ID3D12GraphicsCommandList* cmdList, const MeshConstants& baseConstants, UINT frameIndex,
    UINT instanceCount, UINT partsPerInstance) {
    if (geometry_.IndexCount() == 0 || instanceCount == 0 || partsPerInstance == 0) return;
    const UINT f = frameIndex % kFrames;
    MeshConstants base = BindInstancedMultiPart(cmdList, baseConstants, f, partsPerInstance);
    UINT cbIndex = 0;
    const UINT cbLimit = constantBuffer_.Capacity(f) > 1 ? constantBuffer_.Capacity(f) - 2 : 0;
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> constantBufferAddresses;
    constantBufferAddresses.reserve(geometry_.Submeshes().size());
    cmdList->SetPipelineState(shared_->InstancedMultiPartFadeDepth());
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        // 元から半透明のsubmesh(窓ガラス等)はRecordTransparentInstancedMultiPartが
        // 三角形単位で描く。ここで一緒に描くと材質αが落ちて車体と同じ不透明度に
        // なってしまうため除外する。
        if (submesh.IsTransparent()) continue;
        // スロットが尽きたらそこで打ち切る(returnしない)。深度だけ書いてカラー
        // パスを飛ばすと、車両が色を持たない遮蔽物として残ってしまう。
        if (cbIndex >= cbLimit) break;
        base.materialIndex = submesh.materialIndex;
        const D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress =
            WriteMeshConstants(base, f, cbIndex++, submesh.materialIndex, submesh.alphaTestClip);
        constantBufferAddresses.push_back(constantBufferAddress);
        cmdList->SetGraphicsRootConstantBufferView(0, constantBufferAddress);
        cmdList->DrawIndexedInstanced(submesh.indexCount, instanceCount, submesh.indexOffset, 0, 0);
    }
    cmdList->SetPipelineState(shared_->InstancedMultiPartFadeColor());
    size_t constantBufferIndex = 0;
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        if (submesh.IsTransparent()) continue;
        if (constantBufferIndex >= constantBufferAddresses.size()) break;
        cmdList->SetGraphicsRootConstantBufferView(0, constantBufferAddresses[constantBufferIndex++]);
        cmdList->DrawIndexedInstanced(submesh.indexCount, instanceCount, submesh.indexOffset, 0, 0);
    }
}

void MeshPipeline::RecordHighlightSelf(ID3D12GraphicsCommandList* cmdList,
                                       const MeshConstants& baseConstants, UINT frameIndex,
                                       const float tintColor[4]) {
    if (geometry_.IndexCount() == 0) return;
    UINT f = frameIndex % kFrames;

    shared_->BindRootSignature(rs::rhi::CommandList{cmdList}, baseConstants, {srvHeap_, srvSize_});
    dynamicBuffers_.BindInstanceMatOverrideTable(cmdList, f);
    cmdList->SetPipelineState(shared_->Highlight());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &geometry_.VertexBufferView());
    cmdList->IASetIndexBuffer(&geometry_.IndexBufferView());

    // gPartWorld(フレームfの1枚)はSceneRenderer::Tick経由で毎フレーム既に
    // 最新の姿勢(mover/Joint込み)へ更新済み。ここで書き直す必要はない。
    {
        D3D12_GPU_DESCRIPTOR_HANDLE partWorldHandle =
            srvHeap_->GetGPUDescriptorHandleForHeapStart();
        partWorldHandle.ptr += SIZE_T(dynamicBuffers_.FirstPartWorldIndex() + f) * srvSize_;
        cmdList->SetGraphicsRootDescriptorTable(2, partWorldHandle);
    }

    // このスロットは同一フレーム内で既にRecordOpaque(このメッシュの通常描画)が
    // [0..cbCapacity_[f]-2)を使用済みなので、cbCapacity_[f]-1(RecordTransparentの
    // baseSlotとは別、cbCapacity_[f]-2)を専用に使う。cbSlot=0等の共有スロットへ
    // 書くとGPU実行時に既存submesh0のDrawが参照する定数を上書きするtorn read
    // になる。
    MeshConstants tinted = baseConstants;
    tinted.diffuse[0] = tintColor[0];
    tinted.diffuse[1] = tintColor[1];
    tinted.diffuse[2] = tintColor[2];
    tinted.diffuse[3] = tintColor[3];
    const UINT cbSlot = constantBuffer_.Capacity(f) > 0 ? constantBuffer_.Capacity(f) - 1 : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        WriteMeshConstants(tinted, f, cbSlot, static_cast<uint32_t>(materialSet_.Count()), false);
    cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);

    for (const SubMesh& sm : geometry_.Submeshes()) {
        if (sm.IsTransparent()) continue;
        cmdList->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
    }
}

} // namespace rs::render
