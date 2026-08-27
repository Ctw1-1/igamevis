#include "iGameSurfaceNormalsFilter.h"

#include "iGameAttributeSet.h"
#include "iGameCell.h"
#include "iGameFlatArray.h"

#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>

IGAME_NAMESPACE_BEGIN

namespace {

// Newell 方法计算多边形面法向量（未单位化，模长 = 2 * 面积）
Vector3f ComputeFaceNormalNewell(SurfaceMesh* mesh, const igIndex* ptIds, int npts) {
    double nx = 0.0, ny = 0.0, nz = 0.0;
    for (int i = 0; i < npts; ++i) {
        int j = (i + 1) % npts;
        const Point& a = mesh->GetPoint(ptIds[i]);
        const Point& b = mesh->GetPoint(ptIds[j]);
        nx += static_cast<double>(a[1] - b[1]) * static_cast<double>(a[2] + b[2]);
        ny += static_cast<double>(a[2] - b[2]) * static_cast<double>(a[0] + b[0]);
        nz += static_cast<double>(a[0] - b[0]) * static_cast<double>(a[1] + b[1]);
    }
    return Vector3f(static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz));
}

} // anonymous namespace

SurfaceNormalsFilter::SurfaceNormalsFilter() {
    SetNumberOfInputs(1);
    SetNumberOfOutputs(1);
}

bool SurfaceNormalsFilter::Execute() {
    DataObject::Pointer input = GetInput(0);
    if (input == nullptr) return false;

    auto mesh = DynamicCast<SurfaceMesh>(input);
    if (mesh == nullptr) return false;

    const int nFaces  = mesh->GetNumberOfFaces();
    const int nPoints = mesh->GetNumberOfPoints();
    if (nFaces == 0 || nPoints == 0) return false;

    // ParaView / VTK vtkPolyDataNormals 默认参数：
    //   Splitting = ON, FeatureAngle = 30 度
    // 当面片间法向量夹角 > 特征角时，共享顶点被分裂（复制），
    // 每个平滑区域独立计算点法向。
    const float featureAngleCos = 0.8660254f; // cos(30°)

    // -------------------------------------------------------------------
    // 1. 计算每个面的单位法向量（Newell 方法，与 VTK 一致）
    // -------------------------------------------------------------------
    std::vector<Vector3f> faceNormals(nFaces);
    igIndex ptIds[IGAME_CELL_MAX_SIZE]{};

    for (int faceId = 0; faceId < nFaces; ++faceId) {
        int npts = mesh->GetFacePointIds(faceId, ptIds);
        Vector3f n = ComputeFaceNormalNewell(mesh, ptIds, npts);
        float len = static_cast<float>(n.norm());
        if (len > 1e-30f) {
            float invLen = 1.0f / len;
            faceNormals[faceId] = Vector3f(n[0] * invLen, n[1] * invLen, n[2] * invLen);
        } else {
            faceNormals[faceId] = Vector3f(0.0f, 0.0f, 0.0f);
        }
    }

    // -------------------------------------------------------------------
    // 2. 构建边-面邻接，按特征角找面的连通分量（平滑区域）
    //    两个面共享一条边且法向量夹角 <= 特征角 → 同一平滑区域
    // -------------------------------------------------------------------
    mesh->BuildEdges();
    mesh->BuildFaceEdgeLinks();

    std::vector<int> faceComponent(nFaces, -1);
    int numComponents = 0;

    for (int startFace = 0; startFace < nFaces; ++startFace) {
        if (faceComponent[startFace] >= 0) continue;

        std::queue<int> q;
        q.push(startFace);
        faceComponent[startFace] = numComponents;

        while (!q.empty()) {
            int curFace = q.front();
            q.pop();

            igIndex edgeIds[IGAME_CELL_MAX_SIZE]{};
            int nEdges = mesh->GetFaceEdgeIds(curFace, edgeIds);

            for (int e = 0; e < nEdges; ++e) {
                igIndex neighborIds[2]{};
                int nNeighbors = mesh->GetEdgeToNeighborFaces(edgeIds[e], neighborIds);
                for (int k = 0; k < nNeighbors; ++k) {
                    int neighbor = neighborIds[k];
                    if (neighbor < 0 || neighbor == curFace) continue;
                    if (faceComponent[neighbor] >= 0) continue;

                    float dot = faceNormals[curFace][0] * faceNormals[neighbor][0]
                              + faceNormals[curFace][1] * faceNormals[neighbor][1]
                              + faceNormals[curFace][2] * faceNormals[neighbor][2];
                    if (dot >= featureAngleCos) {
                        faceComponent[neighbor] = numComponents;
                        q.push(neighbor);
                    }
                }
            }
        }
        ++numComponents;
    }

    // -------------------------------------------------------------------
    // 3. 特征边分裂：为每个 (连通分量, 原始顶点) 分配新的点 ID
    //    不同平滑区域即使共享原始顶点，也获得独立的新点
    // -------------------------------------------------------------------
    std::vector<std::unordered_map<int, int>> compVertexMap(numComponents);
    std::vector<int> newPtOrigId;
    newPtOrigId.reserve(nPoints);

    std::vector<std::vector<igIndex>> faceNewPtIds(nFaces);

    int newPtCount = 0;
    for (int faceId = 0; faceId < nFaces; ++faceId) {
        int npts = mesh->GetFacePointIds(faceId, ptIds);
        int comp = faceComponent[faceId];
        faceNewPtIds[faceId].resize(npts);
        for (int i = 0; i < npts; ++i) {
            int origPt = static_cast<int>(ptIds[i]);
            auto it = compVertexMap[comp].find(origPt);
            if (it == compVertexMap[comp].end()) {
                compVertexMap[comp][origPt] = newPtCount;
                faceNewPtIds[faceId][i] = newPtCount;
                newPtOrigId.push_back(origPt);
                ++newPtCount;
            } else {
                faceNewPtIds[faceId][i] = static_cast<igIndex>(it->second);
            }
        }
    }

    // -------------------------------------------------------------------
    // 4. 创建新网格的点坐标（复制原始坐标到分裂后的点）
    // -------------------------------------------------------------------
    Points::Pointer newPoints = Points::New();
    newPoints->Reserve(newPtCount);
    for (int i = 0; i < newPtCount; ++i) {
        newPoints->AddPoint(mesh->GetPoint(newPtOrigId[i]));
    }

    // -------------------------------------------------------------------
    // 5. 创建新网格的面连接（使用分裂后的点 ID）
    // -------------------------------------------------------------------
    CellArray::Pointer newFaces = CellArray::New();
    for (int faceId = 0; faceId < nFaces; ++faceId) {
        newFaces->AddCellIds(faceNewPtIds[faceId].data(),
                            static_cast<int>(faceNewPtIds[faceId].size()));
    }

    // -------------------------------------------------------------------
    // 6. 计算点法向量：同一平滑区域内邻接面单位法向的平均，再归一化
    // -------------------------------------------------------------------
    std::vector<Vector3f> newPtNormals(newPtCount, Vector3f(0.0f, 0.0f, 0.0f));
    for (int faceId = 0; faceId < nFaces; ++faceId) {
        int npts = mesh->GetFacePointIds(faceId, ptIds);
        int comp = faceComponent[faceId];
        for (int i = 0; i < npts; ++i) {
            int newPt = compVertexMap[comp][static_cast<int>(ptIds[i])];
            newPtNormals[newPt] += faceNormals[faceId];
        }
    }

    // -------------------------------------------------------------------
    // 7. 组装新 SurfaceMesh 并添加属性
    // -------------------------------------------------------------------
    SurfaceMesh::Pointer newMesh = SurfaceMesh::New();
    newMesh->SetName(mesh->GetName() + "_normals");
    newMesh->SetPoints(newPoints);
    newMesh->SetFaces(newFaces);

    auto newAttrs = newMesh->GetAttributeSet();

    // 删除同名旧属性（如果有）
    while (true) {
        int idx = newAttrs->GetAttributeIndex("Normals");
        if (idx < 0) break;
        newAttrs->DeleteAttribute(idx);
    }
    while (true) {
        int idx = newAttrs->GetAttributeIndex("Normals_Magnitude");
        if (idx < 0) break;
        newAttrs->DeleteAttribute(idx);
    }

    // 面法向量（三分量）+ 模长
    FloatArray::Pointer cellNormals = FloatArray::New();
    cellNormals->SetDimension(3);
    cellNormals->Reserve(nFaces);
    cellNormals->SetName("Normals");

    FloatArray::Pointer cellMag = FloatArray::New();
    cellMag->SetDimension(1);
    cellMag->Reserve(nFaces);
    cellMag->SetName("Normals_Magnitude");

    for (int faceId = 0; faceId < nFaces; ++faceId) {
        const Vector3f& n = faceNormals[faceId];
        cellNormals->AddElement3(n[0], n[1], n[2]);
        cellMag->AddValue(1.0f);
    }
    newAttrs->AddAttribute(IG_NORMAL, IG_CELL, cellNormals);
    newAttrs->AddAttribute(IG_SCALAR, IG_CELL, cellMag);

    // 点法向量（三分量）+ 模长
    FloatArray::Pointer pointNormalsArr = FloatArray::New();
    pointNormalsArr->SetDimension(3);
    pointNormalsArr->Reserve(newPtCount);
    pointNormalsArr->SetName("Normals");

    FloatArray::Pointer pointMag = FloatArray::New();
    pointMag->SetDimension(1);
    pointMag->Reserve(newPtCount);
    pointMag->SetName("Normals_Magnitude");

    for (int i = 0; i < newPtCount; ++i) {
        Vector3f n = newPtNormals[i];
        float len = static_cast<float>(n.norm());
        if (len > 1e-30f) {
            float invLen = 1.0f / len;
            n = Vector3f(n[0] * invLen, n[1] * invLen, n[2] * invLen);
        } else {
            n = Vector3f(0.0f, 0.0f, 0.0f);
        }
        pointNormalsArr->AddElement3(n[0], n[1], n[2]);
        pointMag->AddValue(1.0f);
    }
    newAttrs->AddAttribute(IG_NORMAL, IG_POINT, pointNormalsArr);
    newAttrs->AddAttribute(IG_SCALAR, IG_POINT, pointMag);

    newAttrs->ForceReConvertToDrawableData();
    newAttrs->Modified();
    newMesh->Modified();

    SetOutput(0, newMesh);
    return true;
}

IGAME_NAMESPACE_END
