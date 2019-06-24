#include "LGHDemo.h"
#include "tinyxml/tinyxml.h"

#ifdef _WIN32
#define COMPARE(a,b) (_stricmp(a,b)==0)
#else
#define COMPARE(a,b) (strcasecmp(a,b)==0)
#endif

void ReadFloat(TiXmlElement *element, float  &f, const char *name = "value");
void ReadVector(TiXmlElement *element, glm::vec3 &v);

bool ParseSceneFile(const std::string SceneFile, std::string& modelPath, Camera& m_Camera, bool& isCameraInitialized,
	float& sunIntensity, float& sunOrientation, float& sunInclination, int& imgWidth, int &imgHeight)
{
	TiXmlDocument doc(SceneFile.c_str());
	if (!doc.LoadFile()) {
		printf("Failed to load the file \"%s\"\n", SceneFile);
		return 0;
	}

	TiXmlElement *xml = doc.FirstChildElement("scenedescription");
	if (!xml) {
		printf("No \"scenedescription\" tag found.\n");
		return 0;
	}

	TiXmlElement *model = xml->FirstChildElement("model");
	if (!model) 
	{
		printf("No \"model\" tag found.\n");
		return 0;
	}
	else
	{
		const char* path = model->Attribute("path");
		if (path)
		{
			modelPath = path;
		}
		else
		{
			printf("No \"path\" attribute found in \"model\" tag.\n");
			return 0;
		}
	}

	// default values
	glm::vec3 pos(0,0,0), dir(0,-1,0), up(0,1,0);
	float fov = 1, nearClip = 1.0, farClip = 1000.0;
	imgWidth = 1280, imgHeight = 720;

	isCameraInitialized = false;
	TiXmlElement *cam = xml->FirstChildElement("camera");
	if (cam) 
	{
		isCameraInitialized = true;
		TiXmlElement *camChild = cam->FirstChildElement();
		while (camChild) 
		{
			if (COMPARE(camChild->Value(), "position")) ReadVector(camChild, pos);
			else if (COMPARE(camChild->Value(), "target")) ReadVector(camChild, dir);
			else if (COMPARE(camChild->Value(), "up")) ReadVector(camChild, up);
			else if (COMPARE(camChild->Value(), "fov")) ReadFloat(camChild, fov);
			else if (COMPARE(camChild->Value(), "width")) camChild->QueryIntAttribute("value", &imgWidth);
			else if (COMPARE(camChild->Value(), "height")) camChild->QueryIntAttribute("value", &imgHeight);
			else if (COMPARE(camChild->Value(), "near")) ReadFloat(camChild, nearClip);
			else if (COMPARE(camChild->Value(), "far")) ReadFloat(camChild, farClip);
			camChild = camChild->NextSiblingElement();
		}
		m_Camera.SetEyeAtUp(Vector3(pos.x, pos.y, pos.z), Vector3(pos.x+dir.x,pos.y+dir.y,pos.z+dir.z), Vector3(up.x,up.y,up.z));
		m_Camera.SetFOV(fov * PI / 180);
		m_Camera.SetZRange(nearClip, farClip);
	}


	TiXmlElement *light = xml->FirstChildElement("light");
	// default values
	sunIntensity = 3.0;
	sunOrientation = 1.16;
	sunInclination = 0.86;

	if (light) 
	{
		const char* type = light->Attribute("type");
		if (type)
		{
			if (!COMPARE(type, "directional"))
			{
				printf("Current version only supports directional lights.\n");
				return 0;
			}
		}

		TiXmlElement *lightChild = light->FirstChildElement();
		while (lightChild) 
		{
			if (COMPARE(lightChild->Value(), "intensity")) ReadFloat(lightChild, sunIntensity);
			else if (COMPARE(lightChild->Value(), "orientation")) ReadFloat(lightChild, sunOrientation);
			else if (COMPARE(lightChild->Value(), "inclination")) ReadFloat(lightChild, sunInclination);
			lightChild = lightChild->NextSiblingElement();
		}
	}

	return 1;
}

void ReadFloat(TiXmlElement *element, float &f, const char *name)
{
	double d = (double)f;
	element->QueryDoubleAttribute(name, &d);
	f = (float)d;
}

void ReadVector(TiXmlElement *element, glm::vec3 &v)
{
	double x = (double)v.x;
	double y = (double)v.y;
	double z = (double)v.z;
	element->QueryDoubleAttribute("x", &x);
	element->QueryDoubleAttribute("y", &y);
	element->QueryDoubleAttribute("z", &z);
	v.x = (float)x;
	v.y = (float)y;
	v.z = (float)z;

	float f = 1;
	ReadFloat(element, f);
	v *= f;
}
