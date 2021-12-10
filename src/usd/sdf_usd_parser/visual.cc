/*
 * Copyright (C) 2021 Open Source Robotics Foundation
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
 *
*/

#include "sdf_usd_parser/visual.hh"

#include <iostream>
#include <string>

#include <ignition/common/URI.hh>
#include <ignition/common/Util.hh>
#include <ignition/common/MeshManager.hh>
#include <ignition/common/Mesh.hh>

#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

#include "sdf/Visual.hh"
#include "sdf/Mesh.hh"
#include "sdf_usd_parser/geometry.hh"
#include "sdf_usd_parser/material.hh"
#include "sdf_usd_parser/utils.hh"

namespace usd
{
  bool ParseSdfVisual(const sdf::Visual &_visual, pxr::UsdStageRefPtr &_stage,
      const std::string &_path)
  {
    const pxr::SdfPath sdfVisualPath(_path);
    auto usdVisualXform = pxr::UsdGeomXform::Define(_stage, sdfVisualPath);
    usd::SetPose(_visual.RawPose(), _stage, sdfVisualPath);

    const auto geometry = *(_visual.Geom());
    const auto geometryPath = std::string(_path + "/geometry");
    std::cerr << "_visual " << _visual.Name() << '\n';
    std::cerr << "geometryPath " << geometryPath << '\n';
    if (!ParseSdfGeometry(geometry, _stage, geometryPath))
    {
      std::cerr << "Error parsing geometry attached to visual ["
                << _visual.Name() << "]\n";
      return false;
    }

    auto geomPrim = _stage->GetPrimAtPath(pxr::SdfPath(geometryPath));
    if (geomPrim)
    {
      // // auto bindMaterial = pxr::UsdShadeMaterialBindingAPI(geomPrim);
      // // pxr::UsdRelationship directBindingRel =
      // //   bindMaterial.GetDirectBindingRel();
      // //
      // // pxr::SdfPathVector paths;
      // // directBindingRel.GetTargets(&paths);
      // // std::cerr << "paths " << paths.size() << '\n';
      // // std::cerr << " bindMaterial.GetCollectionBindingRels() " << bindMaterial.GetCollectionBindingRels().size() << '\n';
      // // if (bindMaterial.GetCollectionBindingRels().size() == 0)
      // // {
      // bool bindMaterial = true;
      // if (geometry.Type() == sdf::GeometryType::MESH)
      // {
      //   ignition::common::URI uri(geometry.MeshShape()->Uri());
      //   std::string fullname;
      //   // std::cerr << "_geometry.MeshShape()->Uri() " << _geometry.MeshShape()->Uri() << '\n';
      //   if (uri.Scheme() == "https" || uri.Scheme() == "http")
      //   {
      //     fullname =
      //       ignition::common::findFile(uri.Str());
      //   }
      //   else
      //   {
      //     fullname =
      //       ignition::common::findFile(geometry.MeshShape()->Uri());
      //   }
      //   // std::cerr << "fullName" << fullname << '\n';
      //
      //   auto ignMesh = ignition::common::MeshManager::Instance()->Load(
      //       fullname);
      //   if (ignMesh->MaterialCount() > 0)
      //   {
      //     bindMaterial = false;
      //   }
      // }
      // if (bindMaterial)
      // {
      //
        const auto material = _visual.Material();
        pxr::UsdShadeMaterial materialUSD;

        if (material)
        {
          materialUSD = ParseSdfMaterial(material, _stage);
          if (!materialUSD)
          {
            std::cerr << "Error parsing material attached to visual ["
                      << _visual.Name() << "]\n";
            return false;
          }
        }

        // std::cerr << "bind material 2" << '\n';
        pxr::UsdShadeMaterialBindingAPI(geomPrim).Bind(materialUSD);
      // }
    }

    return true;
  }
}
