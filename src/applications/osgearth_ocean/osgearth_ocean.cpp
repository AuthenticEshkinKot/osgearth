/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2009 Pelican Ventures, Inc.
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include <osgEarth/Notify>

#include <osgUtil/Optimizer>
#include <osgDB/ReadFile>
#include <osgViewer/Viewer>

#include <osg/Material>
#include <osg/Geode>
#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/Projection>
#include <osg/AutoTransform>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/CullFace>

#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>
#include <osgGA/SphericalManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>



#include <osgTerrain/TerrainTile>
#include <osgTerrain/GeometryTechnique>

#include <osgDB/WriteFile>

#include <osgText/Text>

#include <iostream>

using namespace osg;
using namespace osgDB;
using namespace osgTerrain;

char fnormal_source[] = 
"vec3 fnormal(void)\n"
"{\n"
"    //Compute the normal \n"
"    vec3 normal = gl_NormalMatrix * gl_Normal;\n"
"    normal = normalize(normal);\n"
"    return normal;\n"
"}\n";

char directionalLight_source[] = 
"void directionalLight(in int i, \n"
"                      in vec3 normal, \n"
"                      inout vec4 ambient, \n"
"                      inout vec4 diffuse, \n"
"                      inout vec4 specular) \n"
"{ \n"
"   float nDotVP;         // normal . light direction \n"
"   float nDotHV;         // normal . light half vector \n"
"   float pf;             // power factor \n"
" \n"
"   nDotVP = max(0.0, dot(normal, normalize(vec3 (gl_LightSource[i].position)))); \n"
"   nDotHV = max(0.0, dot(normal, vec3 (gl_LightSource[i].halfVector))); \n"
" \n"
"   if (nDotVP == 0.0) \n"
"   { \n"
"       pf = 0.0; \n"
"   } \n"
"   else \n"
"   { \n"
"       pf = pow(nDotHV, gl_FrontMaterial.shininess); \n"
" \n"
"   } \n"
"   ambient  += gl_LightSource[i].ambient; \n"
"   diffuse  += gl_LightSource[i].diffuse * nDotVP; \n"
"   specular += gl_LightSource[i].specular * pf; \n"
"} \n";


char vert_source[] =
"varying vec2 texCoord0;\n"
"uniform float osg_SimulationTime; \n"
"uniform mat4 osg_ViewMatrixInverse;\n"
"uniform mat4 osg_ViewMatrix;\n"
"uniform float osgEarth_oceanHeight;\n"
"uniform sampler2D osgEarth_oceanMaskUnit;\n"
"varying vec2 oceanMaskTexCoord;\n"
"\n"
"\n"
"void main (void)\n"
"{\n"
"    {\n"
"    vec4 ambient = vec4(0.0); \n"
"    vec4 diffuse = vec4(0.0); \n"
"    vec4 specular = vec4(0.0); \n"
" \n"
"    vec3 normal = fnormal(); \n"
" \n"
"    directionalLight(0, normal, ambient, diffuse, specular); \n"
"     vec4 color = gl_FrontLightModelProduct.sceneColor + \n"
"                  ambient  * gl_FrontMaterial.ambient + \n"
"                  diffuse  * gl_FrontMaterial.diffuse + \n"
"                  specular * gl_FrontMaterial.specular; \n"
" \n"
"    gl_FrontColor = color; \n"
"   }\n"
"\n"
"   vec4 color = texture2D( osgEarth_oceanMaskUnit, gl_MultiTexCoord1.st); \n"
"   if (color.a < 0.1)\n"
"   {\n"
"     vec4 vert = osg_ViewMatrixInverse * gl_ModelViewMatrix  * gl_Vertex;\n"                    
"     vec3 n = normalize(vec3(vert.x, vert.y, vert.z));\n"
"     float scale = sin(vert.x + osg_SimulationTime * 3.0) * osgEarth_oceanHeight;\n"
"     n = n * scale;\n"
"     vert += vec4(n.x, n.y,n.z,0);\n"
"     vert = osg_ViewMatrix * vert;\n"
"     gl_Position = gl_ProjectionMatrix * vec4(vert.x, vert.y, vert.z, 1.0);\n"
"   }\n"
"   else\n"
"   {\n"
"     gl_Position = ftransform();\n"
"   }\n"
"\n"
"	texCoord0 = gl_MultiTexCoord0.st;\n"
"   oceanMaskTexCoord = gl_MultiTexCoord1.st;\n"
"}\n";

char frag_source[] = "\n"
"varying vec2 texCoord0;\n"
"varying vec2 oceanMaskTexCoord;\n"
"uniform sampler2D osgEarth_Layer0_unit;\n"
"uniform sampler2D osgEarth_oceanMaskUnit;\n"
"void main (void )\n"
"{\n"
"    vec4 tex0 = texture2D(osgEarth_Layer0_unit, texCoord0);\n"
"    vec4 ocean_mask = texture2D(osgEarth_oceanMaskUnit, oceanMaskTexCoord);\n"
//"    vec4 oceanColor = vec4(0,0,1,1);\n"
"    vec4 color = tex0;\n"
//"    if (ocean_mask.a <= 0.1) color = oceanColor;\n"
"    gl_FragColor = gl_Color * color;\n"
"}\n"
"\n";


int main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);

    // construct the viewer.
    osgViewer::Viewer viewer(arguments);

    osg::Group* group = new osg::Group;

    osg::ref_ptr<osg::Node> loadedModel = osgDB::readNodeFiles(arguments);

    if (loadedModel.valid())
    {
        std::string shaderSource = std::string(fnormal_source) + std::string(directionalLight_source) + std::string(vert_source);
        osg::Shader* vert_shader = new osg::Shader( osg::Shader::VERTEX, shaderSource );
        osg::Shader* frag_shader = new osg::Shader( osg::Shader::FRAGMENT, frag_source );
        osg::Program* program = new osg::Program();

        osg::Uniform* unit0Uniform = loadedModel->getOrCreateStateSet()->getOrCreateUniform("osgEarth_Layer0_unit", osg::Uniform::INT);
        unit0Uniform->set( 0 );

        osg::Uniform* unit1Uniform = loadedModel->getOrCreateStateSet()->getOrCreateUniform("osgEarth_oceanMaskUnit", osg::Uniform::INT);
        unit1Uniform->set( 1 );

        osg::Uniform* oceanHeightUniform = loadedModel->getOrCreateStateSet()->getOrCreateUniform("osgEarth_oceanHeight", osg::Uniform::FLOAT);
        oceanHeightUniform->set( 250.0f );


        program->addShader( vert_shader );
        program->addShader( frag_shader);
        loadedModel.get()->getOrCreateStateSet()->setAttributeAndModes(program, osg::StateAttribute::ON);
        group->addChild(loadedModel.get());
    }

    loadedModel->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);

    {
        osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;

        keyswitchManipulator->addMatrixManipulator( '1', "Trackball", new osgGA::TrackballManipulator() );
        keyswitchManipulator->addMatrixManipulator( '2', "Flight", new osgGA::FlightManipulator() );
        keyswitchManipulator->addMatrixManipulator( '3', "Drive", new osgGA::DriveManipulator() );
        keyswitchManipulator->addMatrixManipulator( '4', "Terrain", new osgGA::TerrainManipulator() );
        keyswitchManipulator->addMatrixManipulator( '5', "Spherical", new osgGA::SphericalManipulator() );

        std::string pathfile;
        char keyForAnimationPath = '6';
        while (arguments.read("-p",pathfile))
        {
            osgGA::AnimationPathManipulator* apm = new osgGA::AnimationPathManipulator(pathfile);
            if (apm || !apm->valid()) 
            {
                unsigned int num = keyswitchManipulator->getNumMatrixManipulators();
                keyswitchManipulator->addMatrixManipulator( keyForAnimationPath, "Path", apm );
                keyswitchManipulator->selectMatrixManipulator(num);
                ++keyForAnimationPath;
            }
        }

        viewer.setCameraManipulator( keyswitchManipulator.get() );
    }

    // add the state manipulator
    viewer.addEventHandler( new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()) );

        // add the stats handler
    viewer.addEventHandler(new osgViewer::StatsHandler);


    // set the scene to render
    viewer.setSceneData(group);

    // run the viewers frame loop
    return viewer.run();
}
