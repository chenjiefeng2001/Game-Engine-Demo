using System;

namespace AvaloniaEditor.Services;

/// <summary>
/// Slice D **D-C5**：`PickResult` → 场景索引的**唯一**解析 seam。
///
/// 为什么需要 seam 而不是在 ViewModel 里写一行：
///   - 身份映射是**契约**，不是实现细节：`entity_id` 是 `GameObject::ID`
///     （进程级 assign-once），场景索引是 `Scene::GetObjects()` 的位置（会随删除前移）。
///     两者**没有**稳定的算术关系，任何"推导"都会静默选中邻居实体；
///   - 所有策略（可解析 / 不可解析 / 越界）必须集中在一个可被 gate 替换的位置。
///
/// v1.4 / P-1 起，索引由 native 在**产生结果的那一帧**解析并放入
/// <c>PickResultPacket.entity_index</c>，因此本 seam 的实现只是"读字段 + 校验"，
/// 而不是"猜"。
/// </summary>
public interface IPickIdentityResolver
{
    /// <summary>该 seam 当前是否具备解析能力（gate 用它区分"跳过"与"通过"）。</summary>
    bool IsAvailable { get; }

    /// <summary>
    /// 解析 pick 命中的实体身份。
    /// 返回 <c>false</c> 时**不得**改变 UI 选中状态；<paramref name="diagnostic"/>
    /// 必须说明原因（不允许空字符串）。
    /// </summary>
    bool TryResolve(PickResult result, out int sceneIndex, out string diagnostic);
}

/// <summary>
/// 生产实现：使用 v1.4 的 <c>entity_index</c> 字段。
///
/// 三条 fail-closed 规则：
///   1. 非 <c>HIT</c> 不解析（调用方本来就只应在 HIT 时调用）；
///   2. 索引为 <see cref="PickResult.IndexNone"/> ⇒ 实体在结果产生时已不在场景快照中
///      （native 用 `Scene::FindByID` 判定失败）⇒ 不解析、不猜；
///   3. 索引超出 <c>int</c> 范围（防御性）⇒ 不解析。
///
/// **不再存在**"临时 Unavailable"实现：P-1 已批准并落地，
/// 若这里退化回"不可用"，gate P5/P8b 会立刻失败。
/// </summary>
public sealed class PacketPickIdentityResolver : IPickIdentityResolver
{
    public bool IsAvailable => true;

    public bool TryResolve(PickResult result, out int sceneIndex, out string diagnostic)
    {
        sceneIndex = -1;
        if (result is null)
        {
            diagnostic = "no pick result";
            return false;
        }
        if (!result.IsHit)
        {
            diagnostic = $"result status {result.Status} is not a hit";
            return false;
        }
        if (result.EntityIndex == PickResult.IndexNone)
        {
            // native 的 Scene::FindByID 在结果产生时未找到该实体。
            // 这不是"索引 0"，也不允许退回 entity_id - 1。
            diagnostic =
                $"entity id {result.EntityId} was rendered but is no longer in the scene "
                + "snapshot (native FindByID failed); refusing to guess an index";
            return false;
        }
        if (result.EntityIndex > int.MaxValue)
        {
            diagnostic = $"entity index {result.EntityIndex} is outside the supported range";
            return false;
        }

        sceneIndex = (int)result.EntityIndex;
        diagnostic = string.Empty;
        return true;
    }
}
