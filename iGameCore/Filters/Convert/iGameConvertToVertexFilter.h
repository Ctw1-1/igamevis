// ============================================================================
// ConvertToVertexFilter  — 将任意点集/网格转换为顶点单元（IG_VERTEX）网格
// 骨架由 scripts/igame_new_filter.py 生成，算法部分手写。
// 数据流: point_set -> unstructured_mesh   输入端口 1 / 输出端口 1
// ============================================================================
#pragma once

#include <iGameFilter.h>
#include <iGamePointSet.h>
#include <iGameUnstructuredMesh.h>

IGAME_NAMESPACE_BEGIN
class ConvertToVertexFilter : public Filter {
public:
    I_OBJECT(ConvertToVertexFilter);
    static Pointer New() { return new ConvertToVertexFilter; }

    bool Execute() override;

    // 转换模式（多模式框架，后续模式在此扩展）
    enum ConvertMethod {
        INVALID = -1,
        IG_CONVERT_POINT_TO_VERTEX = 0, // 默认：每个输入点 -> 一个 IG_VERTEX 顶点单元，忽略原单元
    };
    void SetConvertMethod(ConvertMethod method) { m_ConvertMethod = method; }
    ConvertMethod GetConvertMethod() const { return m_ConvertMethod; }

protected:
    ConvertToVertexFilter();
    ~ConvertToVertexFilter() override = default;

private:
    // 每个输入点复制为输出点，并生成一个引用该点的 IG_VERTEX 单元；
    // 点属性原样拷贝为点属性；同名同值数组同时作为单元属性挂到顶点单元上
    // （顶点单元 i 只含点 i，单元值即对应点的值）；原单元属性因数量无法对应而丢弃。
    bool ExecutePointToVertex(PointSet::Pointer in, UnstructuredMesh::Pointer out);

    ConvertMethod m_ConvertMethod{IG_CONVERT_POINT_TO_VERTEX};
};
IGAME_NAMESPACE_END
