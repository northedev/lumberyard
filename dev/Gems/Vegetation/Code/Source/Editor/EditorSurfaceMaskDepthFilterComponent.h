/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#pragma once

#include <Components/SurfaceMaskDepthFilterComponent.h>
#include <Vegetation/Editor/EditorVegetationComponentBase.h>

namespace Vegetation
{
    class EditorSurfaceMaskDepthFilterComponent
        : public EditorVegetationComponentBase<SurfaceMaskDepthFilterComponent, SurfaceMaskDepthFilterConfig>
    {
    public:
        using BaseClassType = EditorVegetationComponentBase<SurfaceMaskDepthFilterComponent, SurfaceMaskDepthFilterConfig>;
        AZ_EDITOR_COMPONENT(EditorSurfaceMaskDepthFilterComponent, EditorSurfaceMaskDepthFilterComponentTypeId, BaseClassType);
        static void Reflect(AZ::ReflectContext* context);

        static constexpr const char* const s_categoryName = "Vegetation Filters";
        static constexpr const char* const s_componentName = "Vegetation Surface Mask Depth Filter";
        static constexpr const char* const s_componentDescription = "Limits vegetation to only place within a specified depth between two surface tags";
        static constexpr const char* const s_icon = "Editor/Icons/Components/VegetationFilter.svg";
        static constexpr const char* const s_viewportIcon = "Editor/Icons/Components/Viewport/VegetationFilter.png";
        static constexpr const char* const s_helpUrl = "https://docs.aws.amazon.com/console/lumberyard/vegetationfilters/vegetation-surface-mask-depth-filter";
    };
}
