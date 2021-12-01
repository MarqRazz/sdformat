/*
 * Copyright 2021 Open Source Robotics Foundation
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

#include "parser_usd.hh"

#include "XmlUtils.hh"
#include "SDFExtension.hh"

#include "sdf/Box.hh"
#include "sdf/Camera.hh"
#include "sdf/Cylinder.hh"
#include "sdf/Lidar.hh"
#include "sdf/Mesh.hh"
#include "sdf/Sensor.hh"
#include "sdf/Sphere.hh"
#include "sdf/Joint.hh"
#include "sdf/Pbr.hh"

using namespace sdf;

namespace sdf {
inline namespace SDF_VERSION_NAMESPACE {

  const int g_outputDecimalPrecision = 16;
  const char kLumpPrefix[] = "";
  const char kVisualExt[] = "_visual";
  const char kCollisionExt[] = "_collision";

  typedef std::shared_ptr<SDFExtension> SDFExtensionPtr;

  typedef std::map<std::string, std::vector<SDFExtensionPtr> >
    StringSDFExtensionPtrMap;


  USD2SDF::USD2SDF()
  {
    g_fixedJointsTransformedInRevoluteJoints.clear();
    g_fixedJointsTransformedInFixedJoints.clear();
    g_enforceLimits = true;
    g_reduceFixedJoints = false;
    // g_extensions.clear();
  }

  ////////////////////////////////////////////////////////////////////////////////
  USD2SDF::~USD2SDF()
  {

  }

  ////////////////////////////////////////////////////////////////////////////////
  std::string USD2SDF::GetKeyValueAsString(tinyxml2::XMLElement* _elem)
  {
    std::string valueStr;
    if (_elem->Attribute("value"))
    {
      valueStr = _elem->Attribute("value");
    }
    else if (_elem->FirstChild())
    {
      // Check that this node is a XMLText
      if (_elem->FirstChild()->ToText())
      {
        valueStr = _elem->FirstChild()->Value();
      }
      else
      {
        sdferr << "Attribute value string not set\n";
      }
    }
    return trim(valueStr);
  }


  /////////////////////////////////////////////////
  /// \brief convert Vector3 to string
  /// \param[in] _vector a ignition::math::Vector3d
  /// \return a string
  std::string USD2SDF::Vector32Str(const ignition::math::Vector3d _vector)
  {
    std::stringstream ss;
    ss << _vector.X();
    ss << " ";
    ss << _vector.Y();
    ss << " ";
    ss << _vector.Z();
    return ss.str();
  }

  ////////////////////////////////////////////////////////////////////////////////
  std::string USD2SDF::Values2str(unsigned int _count, const double *_values)
  {
    std::stringstream ss;
    ss.precision(g_outputDecimalPrecision);
    for (unsigned int i = 0 ; i < _count ; ++i)
    {
      if (i > 0)
      {
        ss << " ";
      }
      if (std::fpclassify(_values[i]) == FP_ZERO)
        ss << 0;
      else
        ss << _values[i];
    }
    return ss.str();
  }

  /////////////////////////////////////////////////
  std::string USD2SDF::Values2str(unsigned int _count, const int *_values)
  {
    std::stringstream ss;
    for (unsigned int i = 0 ; i < _count ; ++i)
    {
      if (i > 0)
      {
        ss << " ";
      }
      ss << _values[i];
    }
    return ss.str();
  }

    ////////////////////////////////////////////////////////////////////////////////
    void USD2SDF::AddKeyValue(tinyxml2::XMLElement *_elem, const std::string &_key,
                     const std::string &_value)
    {
      tinyxml2::XMLElement *childElem = _elem->FirstChildElement(_key.c_str());
      if (childElem)
      {
        std::string oldValue = GetKeyValueAsString(childElem);
        if (oldValue != _value)
        {
          sdferr << "multiple inconsistent <" << _key
                  << "> exists due to fixed joint reduction"
                  << " overwriting previous value [" << oldValue
                  << "] with [" << _value << "].\n";
        }
        else
        {
           sdferr << "multiple consistent <" << _key
                  << "> exists with [" << _value
                  << "] due to fixed joint reduction.\n";
        }
        _elem->DeleteChild(childElem);  // remove old _elem
      }

      auto *doc = _elem->GetDocument();
      tinyxml2::XMLElement *ekey = doc->NewElement(_key.c_str());
      tinyxml2::XMLText *textEkey = doc->NewText(_value.c_str());
      ekey->LinkEndChild(textEkey);
      _elem->LinkEndChild(ekey);
    }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateGeometry(tinyxml2::XMLElement* _elem,
                      const Geometry * _geometry)
  {
    auto* doc = _elem->GetDocument();
    tinyxml2::XMLElement *sdfGeometry = doc->NewElement("geometry");

    std::string type;
    tinyxml2::XMLElement *geometryType = nullptr;

    switch (_geometry->Type())
    {
      case sdf::GeometryType::BOX:
        type = "box";
        {
          const sdf::Box * box = _geometry->BoxShape();
          int sizeCount = 3;
          double sizeVals[3];
          sizeVals[0] = box->Size().X();
          sizeVals[1] = box->Size().Y();
          sizeVals[2] = box->Size().Z();
          geometryType = doc->NewElement(type.c_str());
          AddKeyValue(geometryType, "size", Values2str(sizeCount, sizeVals));
        }
        break;
      case sdf::GeometryType::PLANE:
        type = "plane";
        {
          const sdf::Plane * plane = _geometry->PlaneShape();
          geometryType = doc->NewElement(type.c_str());
          ignition::math::Vector3d normalPlane = plane->Normal();
          ignition::math::Vector2d sizePlane = plane->Size();
          double normalV[3];
          normalV[0] = normalPlane[0];
          normalV[1] = normalPlane[1];
          normalV[2] = normalPlane[2];
          double sizeV[2];
          sizeV[0] = sizePlane[0];
          sizeV[1] = sizePlane[1];
          AddKeyValue(geometryType, "normal", Values2str(3, normalV));
          AddKeyValue(geometryType, "size", Values2str(2, sizeV));
        }
        break;
      case sdf::GeometryType::CYLINDER:
        type = "cylinder";
        {
          const sdf::Cylinder * cylinder = _geometry->CylinderShape();
          geometryType = doc->NewElement(type.c_str());
          double length = cylinder->Length();
          double radius = cylinder->Radius();
          AddKeyValue(geometryType, "length", Values2str(1, &length));
          AddKeyValue(geometryType, "radius", Values2str(1, &radius));
        }
        break;
      case sdf::GeometryType::SPHERE:
        type = "sphere";
        {
          const sdf::Sphere * sphere = _geometry->SphereShape();
          geometryType = doc->NewElement(type.c_str());
          double radius = sphere->Radius();
          AddKeyValue(geometryType, "radius", Values2str(1, &radius));
        }
        break;
      case sdf::GeometryType::MESH:
        type = "mesh";
        {
          const sdf::Mesh * mesh = _geometry->MeshShape();
          if (!mesh)
            break;
          geometryType = doc->NewElement(type.c_str());
          // std::cerr << "mesh->Scale() CreateGeom" << mesh->Scale() << '\n';
          AddKeyValue(geometryType, "scale", Vector32Str(mesh->Scale()));
          // do something more to meshes
          {
            // set mesh file
            if (mesh->FilePath().empty())
            {
              sdferr << "usd2sdf: mesh geometry with no filename given.\n";
            }

            // give some warning if file does not exist.
            // disabled while switching to uri
            // @todo: re-enable check
            // std::ifstream fin;
            // fin.open(mesh->filename.c_str(), std::ios::in);
            // fin.close();
            // if (fin.fail())
            //   sdferr << "filename referred by mesh ["
            //          << mesh->filename << "] does not appear to exist.\n";

            // Convert package:// to model://,
            // in ROS, this will work if
            // the model package is in ROS_PACKAGE_PATH and has a manifest.xml
            // as a typical ros package does.
            std::string modelFilename = mesh->FilePath();
            std::string packagePrefix("package://");
            std::string modelPrefix("model://");
            size_t pos1 = modelFilename.find(packagePrefix, 0);
            if (pos1 != std::string::npos)
            {
              size_t repLen = packagePrefix.size();
              modelFilename.replace(pos1, repLen, modelPrefix);
              // sdferr << "ros style uri [package://] is"
              //   << "automatically converted: [" << modelFilename
              //   << "], make sure your ros package is in GAZEBO_MODEL_PATH"
              //   << " and switch your manifest to conform to sdf's"
              //   << " model database format.  See ["
              //   << "http://sdfsim.org/wiki/Model_database#Model_Manifest_XML"
              //   << "] for more info.\n";
            }

            // add mesh filename
            AddKeyValue(geometryType, "uri", modelFilename);
          }
        }
        break;
      default:
        sdferr << "Unknown body type: [" << static_cast<int>(_geometry->Type())
                << "] skipped in geometry\n";
        break;
    }

    if (geometryType)
    {
      sdfGeometry->LinkEndChild(geometryType);
      _elem->LinkEndChild(sdfGeometry);
    }
  }


  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateVisual(tinyxml2::XMLElement *_elem, usd::LinkConstSharedPtr _link,
      std::shared_ptr<sdf::Visual> _visual, const std::string &_oldLinkName)
  {
    auto* doc = _elem->GetDocument();
    // begin create sdf visual node
    tinyxml2::XMLElement *sdfVisual = doc->NewElement("visual");

    // set its name
    if (_oldLinkName.compare(0, _link->name.size(), _link->name) == 0 ||
        _oldLinkName.empty())
    {
      // std::cerr << "/* visual name " << _oldLinkName.c_str() << '\n';
      sdfVisual->SetAttribute("name", _oldLinkName.c_str());
    }
    else
    {
      // std::cerr << "/* visual name " << _link->name + kLumpPrefix + _oldLinkName << '\n';

      sdfVisual->SetAttribute("name",
          (_link->name + kLumpPrefix + _oldLinkName).c_str());
    }

    // add the visualisation transfrom
    double pose[6];
    pose[0] = _visual->RawPose().Pos().X();
    pose[1] = _visual->RawPose().Pos().Y();
    pose[2] = _visual->RawPose().Pos().Z();
    pose[3] = _visual->RawPose().Rot().Roll();
    pose[4] = _visual->RawPose().Rot().Pitch();
    pose[5] = _visual->RawPose().Rot().Yaw();

    AddKeyValue(sdfVisual, "pose", Values2str(6, pose));

    // insert geometry
    if (!_visual || !_visual->Geom())
    {
      sdferr << "usd2sdf: visual of link [" << _link->name
             << "] has no <geometry>.\n";
    }
    else
    {
      CreateGeometry(sdfVisual, _visual->Geom());
    }

    // set additional data from extensions
    // InsertSDFExtensionVisual(sdfVisual, _link->name);

    if (_visual->Material())
    {
      // Refer to this comment in github to understand the ambient and diffuse
      // https://github.com/osrf/sdformat/pull/526#discussion_r623937715
      auto materialTag = sdfVisual->FirstChildElement("material");
      // If the material is not included by an extension then create it
      if (materialTag == nullptr)
      {
        AddKeyValue(sdfVisual, "material", "");
        materialTag = sdfVisual->FirstChildElement("material");
      }

      const sdf::Pbr * pbr = _visual->Material()->PbrMaterial();
      bool isMap = false;
      if(pbr != nullptr)
      {
        const sdf::PbrWorkflow * pbrWorkflow = pbr->Workflow(sdf::PbrWorkflowType::METAL);
        if (pbrWorkflow != nullptr)
        {
          AddKeyValue(materialTag, "pbr", "");
          auto pbrTag = materialTag->FirstChildElement("pbr");
          AddKeyValue(pbrTag, "metal", "");
          auto metalTag = pbrTag->FirstChildElement("metal");

          double roughness = pbrWorkflow->Roughness();
          double metalness = pbrWorkflow->Metalness();
          AddKeyValue(metalTag, "roughness", Values2str(1, &roughness));
          AddKeyValue(metalTag, "metalness", Values2str(1, &metalness));
          if (!pbrWorkflow->MetalnessMap().empty())
          {
            AddKeyValue(metalTag, "metalness_map", pbrWorkflow->MetalnessMap());
            isMap = true;
          }
          if (!pbrWorkflow->AlbedoMap().empty())
          {
            AddKeyValue(metalTag, "albedo_map", pbrWorkflow->AlbedoMap());
            isMap = true;
          }
          if (!pbrWorkflow->NormalMap().empty())
          {
            AddKeyValue(metalTag, "normal_map", pbrWorkflow->NormalMap());
            isMap = true;
          }
          if (!pbrWorkflow->RoughnessMap().empty())
          {
            AddKeyValue(metalTag, "roughness_map", pbrWorkflow->RoughnessMap());
            isMap = true;
          }
        }
      }
      if (isMap)
      {
        double color_diffuse[3];
        color_diffuse[0] = 1;
        color_diffuse[1] = 1;
        color_diffuse[2] = 1;
        AddKeyValue(materialTag, "diffuse", Values2str(3, color_diffuse));
        double color_specular[3];
        color_specular[0] = 1;
        color_specular[1] = 1;
        color_specular[2] = 1;
        AddKeyValue(materialTag, "specular", Values2str(3, color_specular));
      }
      else
      {
        // If the specular and diffuse are defined by an extension then don't
        // use the color values
        if (materialTag->FirstChildElement("diffuse") == nullptr &&
            materialTag->FirstChildElement("ambient") == nullptr)
        {
          if (materialTag->FirstChildElement("diffuse") == nullptr)
          {
            double color_diffuse[4];
            color_diffuse[0] = _visual->Material()->Diffuse().R();
            color_diffuse[1] = _visual->Material()->Diffuse().G();
            color_diffuse[2] = _visual->Material()->Diffuse().B();
            color_diffuse[3] = _visual->Material()->Diffuse().A();
            AddKeyValue(materialTag, "diffuse", Values2str(4, color_diffuse));
          }
          if (materialTag->FirstChildElement("ambient") == nullptr)
          {
            double color_ambient[4];
            color_ambient[0] = _visual->Material()->Ambient().R();
            color_ambient[1] = _visual->Material()->Ambient().G();
            color_ambient[2] = _visual->Material()->Ambient().B();
            color_ambient[3] = _visual->Material()->Ambient().A();
            AddKeyValue(materialTag, "ambient", Values2str(4, color_ambient));
          }
          if (materialTag->FirstChildElement("emissive") == nullptr)
          {
            double color_emissive[4];
            color_emissive[0] = _visual->Material()->Emissive().R();
            color_emissive[1] = _visual->Material()->Emissive().G();
            color_emissive[2] = _visual->Material()->Emissive().B();
            color_emissive[3] = _visual->Material()->Emissive().A();
            AddKeyValue(materialTag, "emissive", Values2str(4, color_emissive));
          }
        }
      }
    }
    // end create _visual node
    _elem->LinkEndChild(sdfVisual);
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateVisuals(tinyxml2::XMLElement* _elem,
                     usd::LinkConstSharedPtr _link)
  {
    // loop through all visuals in
    //   visual_array (urdf 0.3.x)
    //   visual_groups (urdf 0.2.x)
    // and create visual sdf blocks.
    // Note, well as additional visual from
    //   lumped meshes (fixed joint reduction)
    unsigned int visualCount = 0;
    for (std::vector<std::shared_ptr<sdf::Visual>>::const_iterator
        visual = _link->visual_array.begin();
        visual != _link->visual_array.end();
        ++visual)
    {
      // visual sdf has a name if it was lumped/reduced
      // otherwise, use the link name
      std::string visualName = (*visual)->Name();
      if (visualName.empty())
      {
        visualName = _link->name;
      }

      // add _visual extension
      visualName = visualName + kVisualExt;

      if (visualCount > 0)
      {
        std::ostringstream visualNameStream;
        visualNameStream << visualName
                         << "_" << visualCount;
        visualName = visualNameStream.str();
      }

      // make a <visual> block
      CreateVisual(_elem, _link, *visual, visualName);

      ++visualCount;
    }
  }


  ////////////////////////////////////////////////////////////////////////////////
  bool USD2SDF::FixedJointShouldBeReduced(std::shared_ptr<sdf::Joint> _jnt)
  {
      // A joint should be lumped only if its type is fixed and
      // the disabledFixedJointLumping or preserveFixedJoint
      // joint options are not set
      return (_jnt->Type() == sdf::JointType::FIXED &&
                (g_fixedJointsTransformedInRevoluteJoints.find(_jnt->Name()) ==
                   g_fixedJointsTransformedInRevoluteJoints.end()) &&
                (g_fixedJointsTransformedInFixedJoints.find(_jnt->Name()) ==
                   g_fixedJointsTransformedInFixedJoints.end()));
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::InsertSDFExtensionJoint(tinyxml2::XMLElement *_elem,
                               const std::string &_jointName)
  {
    // auto* doc = _elem->GetDocument();
    // for (StringSDFExtensionPtrMap::iterator
    //     sdfIt = g_extensions.begin();
    //     sdfIt != g_extensions.end(); ++sdfIt)
    // {
    //   if (sdfIt->first == _jointName)
    //   {
    //     for (std::vector<SDFExtensionPtr>::iterator
    //         ge = sdfIt->second.begin();
    //         ge != sdfIt->second.end(); ++ge)
    //     {
    //       tinyxml2::XMLElement *physics = _elem->FirstChildElement("physics");
    //       bool newPhysics = false;
    //       if (physics == nullptr)
    //       {
    //         physics = doc->NewElement("physics");
    //         newPhysics = true;
    //       }
    //
    //       tinyxml2::XMLElement *physicsOde = physics->FirstChildElement("ode");
    //       bool newPhysicsOde = false;
    //       if (physicsOde == nullptr)
    //       {
    //         physicsOde = doc->NewElement("ode");
    //         newPhysicsOde = true;
    //       }
    //
    //       tinyxml2::XMLElement *limit = physicsOde->FirstChildElement("limit");
    //       bool newLimit = false;
    //       if (limit == nullptr)
    //       {
    //         limit = doc->NewElement("limit");
    //         newLimit = true;
    //       }
    //
    //       tinyxml2::XMLElement *axis = _elem->FirstChildElement("axis");
    //       bool newAxis = false;
    //       if (axis == nullptr)
    //       {
    //         axis = doc->NewElement("axis");
    //         newAxis = true;
    //       }
    //
    //       tinyxml2::XMLElement *dynamics = axis->FirstChildElement("dynamics");
    //       bool newDynamics = false;
    //       if (dynamics == nullptr)
    //       {
    //         dynamics = doc->NewElement("dynamics");
    //         newDynamics = true;
    //       }
    //
    //       // insert stopCfm, stopErp, fudgeFactor
    //       if ((*ge)->isStopCfm)
    //       {
    //         AddKeyValue(limit, "cfm", Values2str(1, &(*ge)->stopCfm));
    //       }
    //       if ((*ge)->isStopErp)
    //       {
    //         AddKeyValue(limit, "erp", Values2str(1, &(*ge)->stopErp));
    //       }
    //       if ((*ge)->isSpringReference)
    //       {
    //         AddKeyValue(dynamics, "spring_reference",
    //                     Values2str(1, &(*ge)->springReference));
    //       }
    //       if ((*ge)->isSpringStiffness)
    //       {
    //         AddKeyValue(dynamics, "spring_stiffness",
    //                     Values2str(1, &(*ge)->springStiffness));
    //       }
    //
    //       // insert provideFeedback
    //       if ((*ge)->isProvideFeedback)
    //       {
    //         if ((*ge)->provideFeedback)
    //         {
    //           AddKeyValue(physics, "provide_feedback", "true");
    //           AddKeyValue(physicsOde, "provide_feedback", "true");
    //         }
    //         else
    //         {
    //           AddKeyValue(physics, "provide_feedback", "false");
    //           AddKeyValue(physicsOde, "provide_feedback", "false");
    //         }
    //       }
    //
    //       // insert implicitSpringDamper
    //       if ((*ge)->isImplicitSpringDamper)
    //       {
    //         if ((*ge)->implicitSpringDamper)
    //         {
    //           AddKeyValue(physicsOde, "implicit_spring_damper", "true");
    //           /// \TODO: deprecating cfm_damping, transitional tag below
    //           AddKeyValue(physicsOde, "cfm_damping", "true");
    //         }
    //         else
    //         {
    //           AddKeyValue(physicsOde, "implicit_spring_damper", "false");
    //           /// \TODO: deprecating cfm_damping, transitional tag below
    //           AddKeyValue(physicsOde, "cfm_damping", "false");
    //         }
    //       }
    //
    //       // insert fudgeFactor
    //       if ((*ge)->isFudgeFactor)
    //       {
    //         AddKeyValue(physicsOde, "fudge_factor",
    //                     Values2str(1, &(*ge)->fudgeFactor));
    //       }
    //
    //       if (newDynamics)
    //       {
    //         axis->LinkEndChild(dynamics);
    //       }
    //       if (newAxis)
    //       {
    //         _elem->LinkEndChild(axis);
    //       }
    //
    //       if (newLimit)
    //       {
    //         physicsOde->LinkEndChild(limit);
    //       }
    //       if (newPhysicsOde)
    //       {
    //         physics->LinkEndChild(physicsOde);
    //       }
    //       if (newPhysics)
    //       {
    //         _elem->LinkEndChild(physics);
    //       }
    //
    //       // insert all additional blobs into joint
    //       for (auto blobIt = (*ge)->blobs.begin();
    //           blobIt != (*ge)->blobs.end(); ++blobIt)
    //       {
    //         CopyBlob((*blobIt)->FirstChildElement(), _elem);
    //       }
    //     }
    //   }
    // }
  }

  void USD2SDF::CopyBlob(tinyxml2::XMLElement *_src, tinyxml2::XMLElement *_blob_parent)
  {
    if (_blob_parent == nullptr)
    {
      sdferr << "blob parent is null\n";
      return;
    }

    tinyxml2::XMLNode *clone = DeepClone(_blob_parent->GetDocument(), _src);
    if (clone == nullptr)
    {
      sdferr << "Unable to deep copy blob\n";
    }
    else
    {
      _blob_parent->LinkEndChild(clone);
    }
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::AddTransform(tinyxml2::XMLElement *_elem,
                    const ignition::math::Pose3d &_transform)
  {
    ignition::math::Vector3d e = _transform.Rot().Euler();
    double cpose[6] = { _transform.Pos().X(), _transform.Pos().Y(),
                        _transform.Pos().Z(), e.X(), e.Y(), e.Z() };

    // set geometry transform
    AddKeyValue(_elem, "pose", Values2str(6, cpose));
  }



  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateCollision(tinyxml2::XMLElement* _elem,
                       usd::LinkConstSharedPtr _link,
                       std::shared_ptr<sdf::Collision> _collision,
                       const std::string &_oldLinkName)
  {
    auto* doc = _elem->GetDocument();
    // begin create geometry node, skip if no collision specified
    tinyxml2::XMLElement *sdfCollision = doc->NewElement("collision");

    // std::cerr << "CreateCollision link [" << _link->name
    //           << "] old [" << _oldLinkName
    //           << "]\n";
    // set its name, if lumped, add original link name
    // for meshes in an original mesh, it's likely
    // _link->name + mesh count
    if (_oldLinkName.compare(0, _link->name.size(), _link->name) == 0 ||
        _oldLinkName.empty())
    {
      sdfCollision->SetAttribute("name", _oldLinkName.c_str());
    }
    else
    {
      sdfCollision->SetAttribute("name", (_link->name
          + kLumpPrefix + _oldLinkName).c_str());
    }

    // std::cerr << "collision [" << sdfCollision->Attribute("name") << "]\n";

    // set transform
    double pose[6];
    pose[0] = _collision->RawPose().Pos().X();
    pose[1] = _collision->RawPose().Pos().Y();
    pose[2] = _collision->RawPose().Pos().Z();
    pose[3] = _collision->RawPose().Rot().Roll();
    pose[4] = _collision->RawPose().Rot().Pitch();
    pose[5] = _collision->RawPose().Rot().Yaw();
    AddKeyValue(sdfCollision, "pose", Values2str(6, pose));

    // add geometry block
    if (!_collision || !_collision->Geom())
    {
      sdfdbg << "urdf2sdf: collision of link [" << _link->name
             << "] has no <geometry>.\n";
    }
    else
    {
      CreateGeometry(sdfCollision, _collision->Geom());
    }

    // set additional data from extensions
    // InsertSDFExtensionCollision(sdfCollision, _link->name);

    // add geometry to body
    _elem->LinkEndChild(sdfCollision);
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateCollisions(tinyxml2::XMLElement* _elem,
                        usd::LinkConstSharedPtr _link)
  {
    // loop through all collisions in
    //   collision_array (urdf 0.3.x)
    //   collision_groups (urdf 0.2.x)
    // and create collision sdf blocks.
    // Note, well as additional collision from
    //   lumped meshes (fixed joint reduction)
    unsigned int collisionCount = 0;
    for (std::vector<std::shared_ptr<sdf::Collision>>::const_iterator
        collision = _link->collision_array.begin();
        collision != _link->collision_array.end();
        ++collision)
    {
      sdfdbg << "creating collision for link [" << _link->name
             << "] collision [" << (*collision)->Name() << "]\n";

      // collision sdf has a name if it was lumped/reduced
      // otherwise, use the link name
      std::string collisionName = (*collision)->Name();
      if (collisionName.empty())
      {
        collisionName = _link->name;
      }

      // add _collision extension
      collisionName = collisionName + kCollisionExt;

      if (collisionCount > 0)
      {
        std::ostringstream collisionNameStream;
        collisionNameStream << collisionName
                            << "_" << collisionCount;
        collisionName = collisionNameStream.str();
      }

      // make a <collision> block
      CreateCollision(_elem, _link, *collision, collisionName);

      ++collisionCount;
    }
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateInertial(tinyxml2::XMLElement *_elem,
                      usd::LinkConstSharedPtr _link)
  {
    auto* doc = _elem->GetDocument();
    tinyxml2::XMLElement *inertial = doc->NewElement("inertial");

    // set mass properties
    // check and print a warning message
    // double roll = _link->inertial->origin.Rot().Roll();
    // double pitch = _link->inertial->origin.Rot().Pitch();
    // double yaw = _link->inertial->origin.Rot().Yaw();

    /// add pose
    ignition::math::Pose3d pose = _link->inertial->Pose();
    AddTransform(inertial, pose);

    const ignition::math::MassMatrix3d massMatrix = _link->inertial->MassMatrix();
    // add mass
    double mass = massMatrix.Mass();
    AddKeyValue(inertial, "mass",
                Values2str(1, &mass));

    // add inertia (ixx, ixy, ixz, iyy, iyz, izz)
    double ixx, ixy, ixz, iyy, iyz, izz;
    ixx = massMatrix.Ixx();
    ixy = massMatrix.Ixy();
    ixz = massMatrix.Ixz();
    iyy = massMatrix.Iyy();
    iyz = massMatrix.Iyz();
    izz = massMatrix.Izz();
    tinyxml2::XMLElement *inertia = doc->NewElement("inertia");
    AddKeyValue(inertia, "ixx",
                Values2str(1, &ixx));
    AddKeyValue(inertia, "ixy",
                Values2str(1, &ixy));
    AddKeyValue(inertia, "ixz",
                Values2str(1, &ixz));
    AddKeyValue(inertia, "iyy",
                Values2str(1, &iyy));
    AddKeyValue(inertia, "iyz",
                Values2str(1, &iyz));
    AddKeyValue(inertia, "izz",
                Values2str(1, &izz));
    inertial->LinkEndChild(inertia);

    _elem->LinkEndChild(inertial);
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateJoint(tinyxml2::XMLElement *_root,
                   usd::LinkConstSharedPtr _link,
                   ignition::math::Pose3d &/*_currentTransform*/)
  {
    // compute the joint tag
    std::string jtype;
    jtype.clear();
    if (_link->parent_joint != nullptr)
    {
      switch (_link->parent_joint->Type())
      {
        case sdf::JointType::CONTINUOUS:
        case sdf::JointType::REVOLUTE:
          jtype = "revolute";
          break;
        case sdf::JointType::PRISMATIC:
          jtype = "prismatic";
          break;
        // case sdf::JointType::FLOATING:
        // case sdf::JointType::PLANAR:
          break;
        case sdf::JointType::FIXED:
          jtype = "fixed";
          break;
        default:
          sdferr << "Unknown joint type: ["
                  << static_cast<int>(_link->parent_joint->Type())
                  << "] in link [" << _link->name << "]\n";
          break;
      }
    }

    // If this is a fixed joint and the legacy option disableFixedJointLumping
    // is present and the new option preserveFixedJoint is not, then the fixed
    // joint should be converted to a revolute joint with max and mim position
    // limits set to (0, 0) for backward compatibility
    bool fixedJointConvertedToRevoluteJoint = false;
    if (jtype == "fixed")
    {
      fixedJointConvertedToRevoluteJoint =
        (g_fixedJointsTransformedInRevoluteJoints.find(_link->parent_joint->Name())
         != g_fixedJointsTransformedInRevoluteJoints.end());
    }

    // skip if joint type is fixed and it is lumped
    //   skip/return with the exception of root link being world,
    //   because there's no lumping there
    // std::cerr << "FixedJointShouldBeReduced(_link->parent_joint) " << FixedJointShouldBeReduced(_link->parent_joint) << '\n';
    if (_link->getParent() && _link->getParent()->name != "world"
        && FixedJointShouldBeReduced(_link->parent_joint)
        && g_reduceFixedJoints)
    {
      return;
    }

    if (!jtype.empty())
    {
      auto* doc = _root->GetDocument();
      tinyxml2::XMLElement *joint = doc->NewElement("joint");
      if (jtype == "fixed" && fixedJointConvertedToRevoluteJoint)
      {
        joint->SetAttribute("type", "revolute");
      }
      else
      {
        joint->SetAttribute("type", jtype.c_str());
      }
      joint->SetAttribute("name", _link->parent_joint->Name().c_str());
      // Add joint pose relative to parent link
      AddTransform(
          joint, _link->parent_joint->RawPose());
      auto pose = joint->FirstChildElement("pose");
      std::string relativeToAttr = _link->getParent()->name;
      if ("world" == relativeToAttr )
      {
        relativeToAttr = "__model__";
      }
      pose->SetAttribute("relative_to", relativeToAttr.c_str());

      AddKeyValue(joint, "parent", _link->getParent()->name);
      AddKeyValue(joint, "child", _link->name);

      tinyxml2::XMLElement *jointAxis = doc->NewElement("axis");
      tinyxml2::XMLElement *jointAxisLimit = doc->NewElement("limit");
      tinyxml2::XMLElement *jointAxisDynamics = doc->NewElement("dynamics");
      if (jtype == "fixed" && fixedJointConvertedToRevoluteJoint)
      {
        AddKeyValue(jointAxisLimit, "lower", "0");
        AddKeyValue(jointAxisLimit, "upper", "0");
        AddKeyValue(jointAxisDynamics, "damping", "0");
        AddKeyValue(jointAxisDynamics, "friction", "0");
      }
      else if (jtype != "fixed")
      {
        double jointAxisXyzArray[3] =
        { _link->parent_joint->Axis()->Xyz().X(),
          _link->parent_joint->Axis()->Xyz().Y(),
          _link->parent_joint->Axis()->Xyz().Z()};
        AddKeyValue(jointAxis, "xyz",
                    Values2str(3, jointAxisXyzArray));
        // if (_link->parent_joint->dynamics)
        // {
        double damping = _link->parent_joint->Axis()->Damping();
        double friction = _link->parent_joint->Axis()->Friction();
        AddKeyValue(jointAxisDynamics, "damping",
                    Values2str(1, &damping));
        AddKeyValue(jointAxisDynamics, "friction",
                    Values2str(1, &friction));
        // }

        if (g_enforceLimits)
        {
          if (jtype == "slider")
          {
            double lower = _link->parent_joint->Axis()->Lower();
            double upper = _link->parent_joint->Axis()->Upper();
            AddKeyValue(jointAxisLimit, "lower",
                        Values2str(1, &lower));
            AddKeyValue(jointAxisLimit, "upper",
                        Values2str(1, &upper));
          }
          else if (_link->parent_joint->Type() != sdf::JointType::CONTINUOUS)
          {
            double lowstop  = _link->parent_joint->Axis()->Lower();
            double highstop = _link->parent_joint->Axis()->Upper();
            // enforce ode bounds, this will need to be fixed
            if (lowstop > highstop)
            {
              sdferr << "usd2sdf: revolute joint ["
                      << _link->parent_joint->Name()
                      << "] with limits: lowStop[" << lowstop
                      << "] > highStop[" << highstop
                      << "], switching the two.\n";
              double tmp = lowstop;
              lowstop = highstop;
              highstop = tmp;
              // TODO(ahcorde): const
              // _link->parent_joint->Axis()->SetLower(lowstop);
              // _link->parent_joint->Axis()->SetUpper(highstop);
            }
            double lower = _link->parent_joint->Axis()->Lower();
            double upper = _link->parent_joint->Axis()->Upper();
            AddKeyValue(jointAxisLimit, "lower",
                        Values2str(1, &lower));
            AddKeyValue(jointAxisLimit, "upper",
                        Values2str(1, &upper));
          }
          double effort = _link->parent_joint->Axis()->Effort();
          double velocity = _link->parent_joint->Axis()->MaxVelocity();
          AddKeyValue(jointAxisLimit, "effort",
                      Values2str(1, &effort));
          AddKeyValue(jointAxisLimit, "velocity",
                      Values2str(1, &velocity));
        }
      }

      if (jtype == "fixed" && !fixedJointConvertedToRevoluteJoint)
      {
        doc->DeleteNode(jointAxisLimit);
        jointAxisLimit = 0;
        doc->DeleteNode(jointAxisDynamics);
        jointAxisDynamics = 0;
        doc->DeleteNode(jointAxis);
        jointAxis = 0;
      }
      else
      {
        jointAxis->LinkEndChild(jointAxisLimit);
        jointAxis->LinkEndChild(jointAxisDynamics);
        joint->LinkEndChild(jointAxis);
      }

      // copy sdf extensions data
      InsertSDFExtensionJoint(joint, _link->parent_joint->Name());

      // add joint to document
      _root->LinkEndChild(joint);
    }
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateLink(tinyxml2::XMLElement *_root,
                  usd::LinkConstSharedPtr _link,
                  ignition::math::Pose3d &_currentTransform)
  {
    // create new body
    tinyxml2::XMLElement *elem = _root->GetDocument()->NewElement("link");

    // set body name
    elem->SetAttribute("name", _link->name.c_str());

    // compute global transform
    ignition::math::Pose3d localTransform;
    // this is the transform from parent link to current _link
    // this transform does not exist for the root link
    if (_link->parent_joint)
    {
      // tinyxml2::XMLElement *pose = _root->GetDocument()->NewElement("pose");
      AddTransform(_root, _link->pose);
      tinyxml2::XMLElement * pose = _root->FirstChildElement("pose");
      // if (!_link->visual->PoseRelativeTo().empty())
      // {
      //   if (_link->getParent() != nullptr)
      //   {
      //     pose->SetAttribute("relative_to", _link->getParent()->name.c_str());
      //   }
      // }
      elem->LinkEndChild(pose);
    }
    else
    {
      sdferr << "[" << _link->name << "] has no parent joint\n";

      if (_currentTransform != ignition::math::Pose3d::Zero)
      {
        // create origin tag for this element
        AddTransform(elem, _currentTransform);
      }
    }

    // create new inerial block
    CreateInertial(elem, _link);

    // create new collision block
    CreateCollisions(elem, _link);

    // create new visual block
    CreateVisuals(elem, _link);

    // make a <joint:...> block
    CreateJoint(_root, _link, _currentTransform);

    AddLights(_link->lights_, elem);

    AddSensors(_link->sensors_, elem);

    // add body to document
    _root->LinkEndChild(elem);
  }

  ////////////////////////////////////////////////////////////////////////////////
  void USD2SDF::CreateSDF(tinyxml2::XMLElement *_root,
                 usd::LinkConstSharedPtr _link,
                 const ignition::math::Pose3d &_transform)
  {
    ignition::math::Pose3d _currentTransform = _transform;

    // must have an <inertial> block and cannot have zero mass.
    //  allow det(I) == zero, in the case of point mass geoms.
    // @todo:  keyword "world" should be a constant defined somewhere else
    if (_link->name != "world" &&
        ((!_link->inertial) || ignition::math::equal(_link->inertial->MassMatrix().Mass(), 0.0)))
    {
      if (!_link->child_links.empty())
      {
        sdferr << "usd2sdf: link[" << _link->name
               << "] has no inertia, ["
               << static_cast<int>(_link->child_links.size())
               << "] children links ignored.\n";
      }

      if (!_link->child_joints.empty())
      {
        sdferr << "usd2sdf: link[" << _link->name
               << "] has no inertia, ["
               << static_cast<int>(_link->child_links.size())
               << "] children joints ignored.\n";
      }

      if (_link->parent_joint)
      {
        sdferr << "usd2sdf: link[" << _link->name
               << "] has no inertia, "
               << "parent joint [" << _link->parent_joint->Name()
               << "] ignored.\n";
      }

      sdferr << "usd2sdf: link[" << _link->name
             << "] has no inertia, not modeled in sdf\n";
      return;
    }

    // create <body:...> block for non fixed joint attached bodies
    if ((_link->getParent() && _link->getParent()->name == "world") ||
        !g_reduceFixedJoints ||
        (!_link->parent_joint ||
         !FixedJointShouldBeReduced(_link->parent_joint)))
    {
      CreateLink(_root, _link, _currentTransform);
    }

    // recurse into children
    for (unsigned int i = 0 ; i < _link->child_links.size() ; ++i)
    {
      CreateSDF(_root, _link->child_links[i], _currentTransform);
    }
  }

  ////////////////////////////////////////////////////////////////////////////////
  bool USD2SDF::IsUSD(const std::string &_filename)
  {
    return usd::isUSD(_filename);
  }

  void USD2SDF::AddSensors(
    const std::map<std::string, std::shared_ptr<sdf::Sensor>> &_sensors,
    tinyxml2::XMLElement *attach)
  {
    for (auto & sensor : _sensors)
    {
      std::shared_ptr<sdf::Sensor> sdfSensor = sensor.second;

      std::string sensorType;
      switch (sdfSensor->Type()) {
        case sdf::SensorType::CAMERA:
          sensorType = "camera";
          break;
        case sdf::SensorType::LIDAR:
          sensorType = "gpu_lidar";
          break;
        default:
          sensorType = "";
      }

      std::cerr << "sensorType " << sensorType << '\n';

      if (sensorType.empty())
        return;

      tinyxml2::XMLElement *sensorXML = attach->GetDocument()->NewElement("sensor");

      sensorXML->SetAttribute("type", sensorType.c_str());

      if (sdfSensor->Type() == sdf::SensorType::CAMERA)
      {
        const sdf::Camera * camera = sdfSensor->CameraSensor();
        sensorXML->SetAttribute("name", camera->Name().c_str());

        double pose_value[6];
        pose_value[0] = camera->RawPose().Pos().X();
        pose_value[1] = camera->RawPose().Pos().Y();
        pose_value[2] = camera->RawPose().Pos().Z();
        pose_value[3] = camera->RawPose().Rot().Euler()[0];
        pose_value[4] = camera->RawPose().Rot().Euler()[1];
        pose_value[5] = camera->RawPose().Rot().Euler()[2];
        AddKeyValue(sensorXML, "pose", Values2str(6, pose_value));

        tinyxml2::XMLElement *cameraXML =
          sensorXML->GetDocument()->NewElement("camera");

        double horizontalAperture = camera->HorizontalFov().Radian();
        double focalLength = camera->LensFocalLength();
        double nearClip = camera->NearClip();
        double farClip = camera->FarClip();
        double width = camera->ImageWidth();
        double height = camera->ImageHeight();
        double alwaysOn = 1;
        double visualize = 1;
        double updateRate = 30;
        // AddKeyValue(cameraXML, "horizontal_fov", Values2str(1, &horizontalAperture));
        AddKeyValue(sensorXML, "always_on", Values2str(1, &alwaysOn));
        AddKeyValue(sensorXML, "visualize", Values2str(1, &visualize));
        AddKeyValue(sensorXML, "update_rate", Values2str(1, &updateRate));

        tinyxml2::XMLElement *imageXML =
          cameraXML->GetDocument()->NewElement("image");
        AddKeyValue(imageXML, "width", Values2str(1, &width));
        AddKeyValue(imageXML, "height", Values2str(1, &height));
        cameraXML->LinkEndChild(imageXML);

        tinyxml2::XMLElement *clipXML =
          cameraXML->GetDocument()->NewElement("clip");
        AddKeyValue(clipXML, "near", Values2str(1, &nearClip));
        AddKeyValue(clipXML, "far", Values2str(1, &farClip));
        cameraXML->LinkEndChild(clipXML);

        tinyxml2::XMLElement *customFunctionXML =
          cameraXML->GetDocument()->NewElement("custom_function");
        AddKeyValue(customFunctionXML, "f", Values2str(1, &focalLength));
        cameraXML->LinkEndChild(customFunctionXML);

        sensorXML->LinkEndChild(cameraXML);
        attach->LinkEndChild(sensorXML);
      }
      else if (sdfSensor->Type() == sdf::SensorType::LIDAR)
      {
        const sdf::Lidar * lidar = sdfSensor->LidarSensor();
        sensorXML->SetAttribute("name", sensor.second->Name().c_str());
        double pose_value[6];
        pose_value[0] = sdfSensor->RawPose().Pos().X();
        pose_value[1] = sdfSensor->RawPose().Pos().Y();
        pose_value[2] = sdfSensor->RawPose().Pos().Z();
        pose_value[3] = sdfSensor->RawPose().Rot().Euler()[0];
        pose_value[4] = sdfSensor->RawPose().Rot().Euler()[1];
        pose_value[5] = sdfSensor->RawPose().Rot().Euler()[2];
        AddKeyValue(sensorXML, "pose", Values2str(6, pose_value));

        int alwaysOn = 1;
        AddKeyValue(sensorXML, "alwaysOn", Values2str(1, &alwaysOn));
        int visualize = 1;
        AddKeyValue(sensorXML, "visualize", Values2str(1, &visualize));
        int update_rate = 1;
        AddKeyValue(sensorXML, "update_rate", Values2str(1, &update_rate));
        AddKeyValue(sensorXML, "topic", sensor.second->Name().c_str());

        tinyxml2::XMLElement *rayXML =
          sensorXML->GetDocument()->NewElement("lidar");

        tinyxml2::XMLElement *scanXML =
          rayXML->GetDocument()->NewElement("scan");
        rayXML->LinkEndChild(scanXML);

        tinyxml2::XMLElement *horizontalXML =
          scanXML->GetDocument()->NewElement("horizontal");
        scanXML->LinkEndChild(horizontalXML);
        double hSamples = lidar->HorizontalScanSamples();
        double hResolution = lidar->HorizontalScanResolution();
        double hMin = lidar->HorizontalScanMinAngle().Radian();
        double hMax = lidar->HorizontalScanMaxAngle().Radian();
        AddKeyValue(horizontalXML, "samples", Values2str(1, &hSamples));
        AddKeyValue(horizontalXML, "resolution", Values2str(1, &hResolution));
        AddKeyValue(horizontalXML, "min_angle", Values2str(1, &hMin));
        AddKeyValue(horizontalXML, "max_angle", Values2str(1, &hMax));

        tinyxml2::XMLElement *verticalXML =
          scanXML->GetDocument()->NewElement("vertical");
        scanXML->LinkEndChild(verticalXML);
        double vSamples = lidar->VerticalScanSamples();
        double vResolution = lidar->VerticalScanResolution();
        double vMin = lidar->VerticalScanMinAngle().Radian();
        double vMax = lidar->VerticalScanMaxAngle().Radian();
        AddKeyValue(verticalXML, "samples", Values2str(1, &vSamples));
        AddKeyValue(verticalXML, "resolution", Values2str(1, &vResolution));
        AddKeyValue(verticalXML, "min_angle", Values2str(1, &vMin));
        AddKeyValue(verticalXML, "max_angle", Values2str(1, &vMax));

        tinyxml2::XMLElement *rangeXML =
          rayXML->GetDocument()->NewElement("range");
        rayXML->LinkEndChild(rangeXML);

        double rangeMin = lidar->RangeMin();
        double rangeMax = lidar->RangeMax();
        double rangeResolution = lidar->RangeResolution();
        AddKeyValue(rangeXML, "min", Values2str(1, &rangeMin));
        AddKeyValue(rangeXML, "max", Values2str(1, &rangeMax));
        AddKeyValue(rangeXML, "resolution", Values2str(1, &rangeResolution));

        sensorXML->LinkEndChild(rayXML);
        attach->LinkEndChild(sensorXML);
      }
    }
  }

  void USD2SDF::AddLights(
    const std::map<std::string, std::shared_ptr<sdf::Light>> &_lights,
    tinyxml2::XMLElement *attach)
  {
    for (auto & light : _lights)
    {
      std::shared_ptr<sdf::Light> sdfLight = light.second;
      tinyxml2::XMLElement *lightXML = attach->GetDocument()->NewElement("light");
      lightXML->SetAttribute("name", sdfLight->Name().c_str());

      std::string lightType;
      switch (sdfLight->Type()) {
        case sdf::LightType::DIRECTIONAL:
          lightType = "directional";
          break;
        case sdf::LightType::POINT:
          lightType = "point";
          break;
        case sdf::LightType::SPOT:
          lightType = "spot";
          break;
        default:
          lightType = "invalid";
      }
      lightXML->SetAttribute("type", lightType.c_str());

      double pose_value[6];
      pose_value[0] = sdfLight->RawPose().Pos().X();
      pose_value[1] = sdfLight->RawPose().Pos().Y();
      pose_value[2] = sdfLight->RawPose().Pos().Z();
      pose_value[3] = sdfLight->RawPose().Rot().Euler()[0];
      pose_value[4] = sdfLight->RawPose().Rot().Euler()[1];
      pose_value[5] = sdfLight->RawPose().Rot().Euler()[2];
      AddKeyValue(lightXML, "pose", Values2str(6, pose_value));

      double color_diffuse[4];
      color_diffuse[0] = sdfLight->Diffuse().R();
      color_diffuse[1] = sdfLight->Diffuse().G();
      color_diffuse[2] = sdfLight->Diffuse().B();
      color_diffuse[3] = sdfLight->Diffuse().A();
      AddKeyValue(lightXML, "diffuse", Values2str(4, color_diffuse));

      double color_specular[4];
      color_specular[0] = sdfLight->Specular().R();
      color_specular[1] = sdfLight->Specular().G();
      color_specular[2] = sdfLight->Specular().B();
      color_specular[3] = sdfLight->Specular().A();
      AddKeyValue(lightXML, "specular", Values2str(4, color_specular));

      double intensity = sdfLight->Intensity();
      AddKeyValue(lightXML, "intensity", Values2str(1, &intensity));

      tinyxml2::XMLElement *attenuationXML =
        lightXML->GetDocument()->NewElement("attenuation");

      double range = sdfLight->AttenuationRange();
      double constant = sdfLight->ConstantAttenuationFactor();
      double linear = sdfLight->LinearAttenuationFactor();
      double quadratic = sdfLight->QuadraticAttenuationFactor();
      AddKeyValue(attenuationXML, "range", Values2str(1, &range));
      AddKeyValue(attenuationXML, "constant", Values2str(1, &constant));
      AddKeyValue(attenuationXML, "linear", Values2str(1, &linear));
      AddKeyValue(attenuationXML, "quadratic", Values2str(1, &quadratic));

      double direction[3];
      direction[0] = sdfLight->Direction().X();
      direction[1] = sdfLight->Direction().Y();
      direction[2] = sdfLight->Direction().Z();
      AddKeyValue(lightXML, "direction", Values2str(3, direction));

      if (sdfLight->Type() == sdf::LightType::SPOT)
      {
        tinyxml2::XMLElement *spotXML =
          lightXML->GetDocument()->NewElement("spot");

        double innerAngle = sdfLight->SpotInnerAngle().Radian();
        double outerAngle = sdfLight->SpotOuterAngle().Radian();
        double falloff = sdfLight->SpotFalloff();
        AddKeyValue(spotXML, "inner_angle", Values2str(1, &innerAngle));
        AddKeyValue(spotXML, "outer_angle", Values2str(1, &outerAngle));
        AddKeyValue(spotXML, "falloff", Values2str(1, &falloff));
        lightXML->LinkEndChild(spotXML);
      }

      lightXML->LinkEndChild(attenuationXML);
      attach->LinkEndChild(lightXML);
    }
  }

  //parser_urdf.cc InitModelString
  void USD2SDF::read(const std::string &_filename,
    tinyxml2::XMLDocument* _sdfXmlOut)
  {
    usd::WorldInterfaceSharedPtr worldInterface = usd::parseUSDFile(_filename);
    tinyxml2::XMLElement *world = nullptr;
    tinyxml2::XMLElement *sdf = nullptr;

    if (worldInterface == nullptr)
      return;

    world = _sdfXmlOut->NewElement("world");
    world->SetAttribute("name", worldInterface->_worldName.c_str());

    sdf = _sdfXmlOut->NewElement("sdf");
    sdf->SetAttribute("version", "1.7");

    std::cerr << "worldInterface->_lights " << worldInterface->_lights.size() << '\n';

    AddLights(worldInterface->_lights, world);

    std::vector<usd::ModelInterfaceSharedPtr> robotModels = worldInterface->_models;
    for(auto & robotModel: robotModels)
    {
      if (!robotModel)
      {
        sdferr << "Unable to call parseURDF on robot model\n";
        return;
      }

      // create root element and define needed namespaces
      tinyxml2::XMLElement *robot = _sdfXmlOut->NewElement("model");

      // set model name to urdf robot name if not specified
      robot->SetAttribute("name", robotModel->getName().c_str());

      usd::LinkConstSharedPtr rootLink = robotModel->getRoot();

      ignition::math::Pose3d transform = robotModel->pose;

      // g_extensions.clear();
      g_fixedJointsTransformedInFixedJoints.clear();
      g_fixedJointsTransformedInRevoluteJoints.clear();
      g_initialRobotPose.Set(
        robotModel->pose.Pos().X(),
        robotModel->pose.Pos().Y(),
        robotModel->pose.Pos().Z(),
        robotModel->pose.Rot().Roll(),
        robotModel->pose.Rot().Pitch(),
        robotModel->pose.Rot().Yaw());
      g_initialRobotPoseValid = true;

      std::cerr << "rootLink->name " << rootLink->name << '\n';

      if (rootLink->name == "world")
      {
        // convert all children link
        for (std::vector<usd::LinkSharedPtr>::const_iterator
            child = rootLink->child_links.begin();
            child != rootLink->child_links.end(); ++child)
        {
          std::cerr << "child->name " << (*child)->name << '\n';

          CreateSDF(robot, (*child), transform);
        }
      }
      else
      {
        // convert, starting from root link
        CreateSDF(robot, rootLink, transform);
      }
      // AddLights(robotModel->lights_, world);
      world->LinkEndChild(robot);
    }

    tinyxml2::XMLElement *physicsPluginXML =
      world->GetDocument()->NewElement("plugin");
    physicsPluginXML->SetAttribute("name", "ignition::gazebo::systems::Physics");
    physicsPluginXML->SetAttribute("filename", "ignition-gazebo-physics-system");
    world->LinkEndChild(physicsPluginXML);

    tinyxml2::XMLElement *sensorsPluginXML =
      world->GetDocument()->NewElement("plugin");
    sensorsPluginXML->SetAttribute("name", "ignition::gazebo::systems::Sensors");
    sensorsPluginXML->SetAttribute("filename", "ignition-gazebo-sensors-system");
    world->LinkEndChild(sensorsPluginXML);

    tinyxml2::XMLElement *userCommandsPluginXML =
      world->GetDocument()->NewElement("plugin");
    userCommandsPluginXML->SetAttribute("name", "ignition::gazebo::systems::UserCommands");
    userCommandsPluginXML->SetAttribute("filename", "ignition-gazebo-user-commands-system");
    world->LinkEndChild(userCommandsPluginXML);

    tinyxml2::XMLElement *sceneBroadcasterPluginXML =
      world->GetDocument()->NewElement("plugin");
    sceneBroadcasterPluginXML->SetAttribute("name", "ignition::gazebo::systems::SceneBroadcaster");
    sceneBroadcasterPluginXML->SetAttribute("filename", "ignition-gazebo-scene-broadcaster-system");
    world->LinkEndChild(sceneBroadcasterPluginXML);

    sdf->LinkEndChild(world);

    _sdfXmlOut->LinkEndChild(sdf);
    _sdfXmlOut->SaveFile("salida.xml");
  }
}
}
