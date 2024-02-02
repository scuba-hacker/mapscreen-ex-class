#include "MapScreen_ex.h"
#include "TFT_eSPI.h"

#include <math.h>
#include <cstddef>
#include <memory>

#include "NavigationWaypoints.h"

MapScreen_ex::MapScreen_ex(TFT_eSPI& tft, const MapScreenAttr mapAttributes) : 
                                                        _zoom(1),
                                                        _priorToZoneZoom(1),
                                                        _tileXToDisplay(0),
                                                        _tileYToDisplay(0),
                                                        _showAllLake(false),
                                                        _lastDiverLatitude(0),
                                                        _lastDiverLongitude(0),
                                                        _lastDiverHeading(0),
                                                        _useDiverHeading(true),
                                                        _targetWaypointIndex(-1),
                                                        _prevWaypointIndex(-1),
                                                        _drawAllFeatures(true),
                                                        _tft(tft),
                                                        _mapAttr(mapAttributes),
                                                        _exitWaypointCount(-1)
{
  _currentMap = nullptr;

  _cleanMapAndFeaturesSprite = std::make_unique<TFT_eSprite>(&_tft);
  _compositedScreenSprite = std::make_shared<TFT_eSprite>(&_tft);
  _diverSprite = std::make_unique<TFT_eSprite>(&_tft);
  _diverPlainSprite = std::make_unique<TFT_eSprite>(&_tft);
  _diverRotatedSprite = std::make_unique<TFT_eSprite>(&_tft);
  _featureSprite = std::make_unique<TFT_eSprite>(&_tft);

  _targetSprite = std::make_unique<TFT_eSprite>(&_tft);
  _lastTargetSprite = std::make_unique<TFT_eSprite>(&_tft);
}

void MapScreen_ex::initMapScreen()
{
  initMaps();
  initSprites();
  initExitWaypoints();
}

void MapScreen_ex::initSprites()
{
  _cleanMapAndFeaturesSprite->setColorDepth(16);
  _cleanMapAndFeaturesSprite->createSprite(getTFTWidth(),getTFTHeight());

  _compositedScreenSprite->setColorDepth(16);
  _compositedScreenSprite->createSprite(getTFTWidth(),getTFTHeight());

  _diverSprite->setColorDepth(16);
  _diverSprite->createSprite(_mapAttr.diverSpriteRadius*2,_mapAttr.diverSpriteRadius*2);
  _diverSprite->fillCircle(_mapAttr.diverSpriteRadius,_mapAttr.diverSpriteRadius,_mapAttr.diverSpriteRadius,_mapAttr.diverSpriteColour);
  
  _diverPlainSprite->setColorDepth(16);
  _diverPlainSprite->createSprite(_mapAttr.diverSpriteRadius*2,_mapAttr.diverSpriteRadius*2);
  _diverSprite->pushToSprite(*_diverPlainSprite,0,0);

  _diverSprite->fillCircle(_mapAttr.headingIndicatorOffsetX,_mapAttr.headingIndicatorOffsetY,_mapAttr.headingIndicatorRadius,_mapAttr.headingIndicatorColour);

  _diverRotatedSprite->setColorDepth(16);
  _diverRotatedSprite->createSprite(_mapAttr.diverSpriteRadius*2,_mapAttr.diverSpriteRadius*2);  
  
  _featureSprite->setColorDepth(16);
  _featureSprite->createSprite(_mapAttr.featureSpriteRadius*2+1,_mapAttr.featureSpriteRadius*2+1);
  _featureSprite->fillCircle(_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteColour);

  _targetSprite->setColorDepth(16);
  _targetSprite->createSprite(_mapAttr.featureSpriteRadius*2+1,_mapAttr.featureSpriteRadius*2+1);
  _targetSprite->fillCircle(_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteRadius,_mapAttr.targetSpriteColour);

  _lastTargetSprite->setColorDepth(16);
  _lastTargetSprite->createSprite(_mapAttr.featureSpriteRadius*2+1,_mapAttr.featureSpriteRadius*2+1);
  _lastTargetSprite->fillCircle(_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteRadius,_mapAttr.lastTargetSpriteColour);
}

void MapScreen_ex::initExitWaypoints()
{
  int currentExitIndex=-1;
  
  for (int i=0; i<WraysburyWaypoints::getWaypointsCount(); i++)   // MBJ REFACTOR - needs range and enumerate from C++20 as index used.
  {
    if (strncmp(WraysburyWaypoints::waypoints[i]._label, "Z0", 2) == 0)
    {
      _exitWaypointIndices[++currentExitIndex] = i;          // index i used here
      if (currentExitIndex == s_exitWaypointSize - 1)
      {
        _exitWaypointIndices[currentExitIndex] = -1;
        break;
      }
    }
  }

  _exitWaypointCount = currentExitIndex+1;
}

void MapScreen_ex::initCurrentMap(const double diverLatitude, const double diverLongitude)
{  
  _currentMap = _maps+getAllMapIndex();

  pixel p;
  
  // identify first map that includes diver location within extent
  for (uint8_t i = getFirstDetailMapIndex(); i<=getAllMapIndex(); i++)   // MBJ REFACTOR - needs range and enumerate from C++20 as index used, with break at all map index
  {
    p = convertGeoToPixelDouble(diverLatitude, diverLongitude, _maps[i]);         // index i used here
    if (p.x >= 0 && p.x < getTFTWidth() && p.y >=0 && p.y < getTFTHeight())
    {
      scalePixelForZoomedInTile(p,_tileXToDisplay, _tileYToDisplay);
      _currentMap = _maps+i;
      break;
    }
  }
}

void MapScreen_ex::clearMap()
{
  _currentMap = nullptr;
  _priorToZoneZoom = _zoom = 1;
  _tileXToDisplay = _tileXToDisplay = 0;
  fillScreen(TFT_BLACK);
}

void MapScreen_ex::setTargetWaypointByLabel(const char* label)
{
  _prevWaypointIndex = _targetWaypointIndex;
  _targetWaypointIndex = -1;
  const int numberCharsToCompare = 3;
  // find targetWayPoint in the NavigationWaypoints array by first 3 chars
  for (int i=0; i < WraysburyWaypoints::getWaypointsCount(); i++)        // MBJ REFACTOR - needs range and enumerate from C++20 as index used.
  {
    if (strncmp(WraysburyWaypoints::waypoints[i]._label, label, numberCharsToCompare) == 0)
    {
      _targetWaypointIndex=i;            // index i used here
      break;
    }
  }
}

void MapScreen_ex::setZoom(const int16_t zoom)
{
   if (_showAllLake)
   {
    _showAllLake = false;
    _currentMap = nullptr;
   }

  _zoom = zoom;
//  Serial.printf("switch to zoom %hu normal map\n",zoom);
}
    
void MapScreen_ex::setAllLakeShown(bool showAll)
{ 
  if (_showAllLake && showAll || 
      !_showAllLake && !showAll)
    return;

  if (showAll)
  {
    _showAllLake = true;
    _zoom = 1;
    _currentMap = _maps+getAllMapIndex();
//    Serial.println("setAllLakeShown(true): switch to zoom 1 all lake map\n");
  }
  else
  {
    _showAllLake = false;
    _zoom = 1;
    _currentMap=nullptr;      // force recalculate of currentmap
//    Serial.println("setAllLakeShown(false): switch to zoom 1 normal map\n");
  }
}

void MapScreen_ex::cycleZoom()
{            
  if (_showAllLake)
  {
    _showAllLake = false;
    _zoom = 1;
    _currentMap=nullptr;
//    Serial.println("switch to zoom 1 normal map\n");
  }
  else if (!_showAllLake && _zoom == 4)
  {
    _showAllLake = true;
    _zoom = 1;
    _currentMap = _maps+getAllMapIndex();
 //   Serial.println("switch to zoom 4 normal map\n");
  }
  else if (!_showAllLake && _zoom == 3)
  {
    _showAllLake = false;
    _zoom = 4;
//    Serial.println("switch to zoom 3 normal map\n");
  }
  else if (!_showAllLake && _zoom == 2)
  {
    _showAllLake = false;
    _zoom = 3;
//    Serial.println("switch to zoom 2 normal map\n");
  }
  else if (!_showAllLake && _zoom == 1)
  {
    _showAllLake = false;
    _zoom = 2;
//    Serial.println("switch to zoom 2 normal map\n");
  }
}

const int MapScreen_ex::getClosestJettyIndex(double& shortestDistance)
{
  shortestDistance = 1e10;
  int closestExitWaypointIndex = 255;

  for (int i=0; i<_exitWaypointCount; i++)       // MBJ REFACTOR - needs range and enumerate from C++20 as index used.
  {
    double distance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude, WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._lat, WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._long);
  
    if (distance < shortestDistance)
    {
      shortestDistance =  distance;
      closestExitWaypointIndex = i;   // index i used here
    }
  }
      
  return _exitWaypointIndices[closestExitWaypointIndex];
}

void MapScreen_ex::drawDiverOnBestFeaturesMapAtCurrentZoom(const double diverLatitude, const double diverLongitude, const double diverHeading)
{
  _lastDiverLatitude = diverLatitude;
  _lastDiverLongitude = diverLongitude;
  _lastDiverHeading = diverHeading;
  
  bool forceFirstMapDraw = false;
  if (_currentMap == nullptr)
  {
    initCurrentMap(diverLatitude, diverLongitude);
    forceFirstMapDraw=true;
  }
  
  pixel p = convertGeoToPixelDouble(diverLatitude, diverLongitude, *_currentMap);

  const geo_map* nextMap = getNextMapByPixelLocation(p, _currentMap);

  if (nextMap != _currentMap)
  {
      p = convertGeoToPixelDouble(diverLatitude, diverLongitude, *nextMap);
  }

  int16_t prevTileX = _tileXToDisplay;
  int16_t prevTileY = _tileYToDisplay;
  
  p = scalePixelForZoomedInTile(p,_tileXToDisplay,_tileYToDisplay);

  // draw diver and feature map at pixel
  if (nextMap != _currentMap || prevTileX != _tileXToDisplay || prevTileY != _tileYToDisplay || forceFirstMapDraw)
  {
    if (nextMap->mapData)
    {
      _cleanMapAndFeaturesSprite->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), _zoom, _tileXToDisplay, _tileYToDisplay, 
                                                  nextMap->mapData, nextMap->swapBytes);

      if (_drawAllFeatures)
      {
        drawFeaturesOnCleanMapSprite(*nextMap);
//        drawRegistrationPixelsOnCleanMapSprite(*nextMap);    // Test Pattern
      }
    }
    else
    {
      _cleanMapAndFeaturesSprite->fillSprite(nextMap->backColour);
      drawFeaturesOnCleanMapSprite(*nextMap);  // need to revert zoom to 1
    }
  }

  _cleanMapAndFeaturesSprite->pushToSprite(*_compositedScreenSprite,0,0);

  double distanceToClosestJetty = 0.0;
  double bearing = 0.0;
  bearing = drawDirectionLineOnCompositeSprite(diverLatitude, diverLongitude, *nextMap,getClosestJettyIndex(distanceToClosestJetty), _mapAttr.directionLineColour, _mapAttr.directionLinePixelLength);

  bearing = drawDirectionLineOnCompositeSprite(diverLatitude, diverLongitude, *nextMap,_targetWaypointIndex, _mapAttr.targetLineColour, _mapAttr.targetLinePixelLength);
  
  drawHeadingLineOnCompositeMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);
      
  drawDiverOnCompositedMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);

  writeMapTitleToSprite(*_compositedScreenSprite, *nextMap);
  
  copyFullScreenSpriteToDisplay(*_compositedScreenSprite);

  _currentMap = nextMap;
}

bool MapScreen_ex::isPixelOutsideScreenExtent(const MapScreen_ex::pixel loc) const
{
  return (loc.x <= 0 || loc.x >= getTFTWidth() || loc.y <=0 || loc.y >= getTFTHeight()); 
}

MapScreen_ex::pixel MapScreen_ex::scalePixelForZoomedInTile(const pixel p, int16_t& tileX, int16_t& tileY) const
{
  tileX = p.x / (getTFTWidth() / _zoom);
  tileY = p.y / (getTFTHeight() / _zoom);

  pixel pScaled;
  if (tileX < _zoom && tileY < _zoom)
  {
    pScaled.x = p.x * _zoom - (getTFTWidth())  * tileX;
    pScaled.y = p.y * _zoom - (getTFTHeight()) * tileY;
  }
  else
  {
    pScaled.x = p.x * _zoom;
    pScaled.y = p.y * _zoom;
    tileX = tileY = 0;
  }

  pScaled.colour = p.colour;

//  debugScaledPixelForTile(p, pScaled, tileX,tileY);

  return pScaled;
}

double MapScreen_ex::distanceBetween(double lat1, double long1, double lat2, double long2) const
{
  // returns distance in meters between two positions, both specified
  // as signed decimal-degrees latitude and longitude. Uses great-circle
  // distance computation for hY_t3pothetical sphere of radius 6372795 meters.
  // Because Earth is no exact sphere, rounding errors may be up to 0.5%.
  // Courtesy of Maarten Lamers
  double delta = radians(long1-long2);
  double sdlong = sin(delta);
  double cdlong = cos(delta);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  double slat1 = sin(lat1);
  double clat1 = cos(lat1);
  double slat2 = sin(lat2);
  double clat2 = cos(lat2);
  delta = (clat1 * slat2) - (slat1 * clat2 * cdlong);
  delta = sq(delta);
  delta += sq(clat2 * sdlong);
  delta = sqrt(delta);
  double denom = (slat1 * slat2) + (clat1 * clat2 * cdlong);
  delta = atan2(delta, denom);
  return delta * 6372795.0;
}

double MapScreen_ex::degreesCourseTo(double lat1, double long1, double lat2, double long2) const
{
  return radiansCourseTo( lat1,  long1,  lat2,  long2) / PI * 180;
}

double MapScreen_ex::radiansCourseTo(double lat1, double long1, double lat2, double long2) const
{
  // returns course in degrees (North=0, West=270) from position 1 to position 2,
  // both specified as signed decimal-degrees latitude and longitude.
  // Because Earth is no exact sphere, calculated course may be off by a tiny fraction.
  // Courtesy of Maarten Lamers
  double dlon = radians(long2-long1);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  double a1 = sin(dlon) * cos(lat2);
  double a2 = sin(lat1) * cos(lat2) * cos(dlon);
  a2 = cos(lat1) * sin(lat2) - a2;
  a2 = atan2(a1, a2);
  if (a2 < 0.0)
  {
    a2 += TWO_PI;
  }
  return a2;
}

int MapScreen_ex::drawDirectionLineOnCompositeSprite(const double diverLatitude, const double diverLongitude, 
                                                  const geo_map& featureMap, const int waypointIndex, uint16_t colour, int indicatorLength)
{
  int heading = 0;

  const NavigationWaypoint& w = WraysburyWaypoints::waypoints[waypointIndex];

  pixel pDiver = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  int16_t diverTileX=0,diverTileY=0;
  pDiver = scalePixelForZoomedInTile(pDiver,diverTileX,diverTileY);

  int16_t targetTileX=0,targetTileY=0;
  pixel pTarget = convertGeoToPixelDouble(w._lat, w._long, featureMap);

  if (!isPixelOutsideScreenExtent(convertGeoToPixelDouble(w._lat, w._long, featureMap)))
  {
    // use line between diver and target locations
    pTarget.x = pTarget.x * _zoom - getTFTWidth() * diverTileX;
    pTarget.y = pTarget.y * _zoom - getTFTHeight() * diverTileY;

    _compositedScreenSprite->drawLine(pDiver.x, pDiver.y, pTarget.x,pTarget.y,colour);

    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y-2, pTarget.x,pTarget.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y+2, pTarget.x,pTarget.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y-2, pTarget.x,pTarget.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y+2, pTarget.x,pTarget.y,colour);

    if (pTarget.y < pDiver.y)
      heading = (int)(atan((double)(pTarget.x - pDiver.x) / (double)(-(pTarget.y - pDiver.y))) * 180.0 / PI) % 360;
    else if (pTarget.y > pDiver.y)
      heading = (int)(180.0 + atan((double)(pTarget.x - pDiver.x) / (double)(-(pTarget.y - pDiver.y))) * 180.0 / PI);
  }
  else
  {
    heading = degreesCourseTo(diverLatitude,diverLongitude,w._lat,w._long);

    // use lat/long to draw outside map area with arbitrary length.
    pixel pHeading;
  
    double rads = heading * PI / 180.0;  
    pHeading.x = pDiver.x + indicatorLength * sin(rads);
    pHeading.y = pDiver.y - indicatorLength * cos(rads);

    _compositedScreenSprite->drawLine(pDiver.x, pDiver.y, pHeading.x,pHeading.y,colour);
  
    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y-2, pHeading.x,pHeading.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y+2, pHeading.x,pHeading.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y-2, pHeading.x,pHeading.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y+2, pHeading.x,pHeading.y,colour);
  }

  return heading;
}

void MapScreen_ex::drawHeadingLineOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, 
                                                            const double heading, const geo_map& featureMap)
{
  int16_t tileX=0,tileY=0;
  pixel pDiver = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  pDiver = scalePixelForZoomedInTile(pDiver,tileX,tileY);
  
  const double hY_t3potoneuse=50;
  pixel pHeading;

  double rads = heading * PI / 180.0;  
  pHeading.x = pDiver.x + hY_t3potoneuse * sin(rads);
  pHeading.y = pDiver.y - hY_t3potoneuse * cos(rads);

  _compositedScreenSprite->drawLine(pDiver.x, pDiver.y, pHeading.x,pHeading.y,_mapAttr.diverHeadingColour);

  _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y-2, pHeading.x,pHeading.y,_mapAttr.diverHeadingColour);
  _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y+2, pHeading.x,pHeading.y,_mapAttr.diverHeadingColour);
  _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y-2, pHeading.x,pHeading.y,_mapAttr.diverHeadingColour);
  _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y+2, pHeading.x,pHeading.y,_mapAttr.diverHeadingColour);
}

void MapScreen_ex::drawDiverOnCompositedMapSprite(const double latitude, const double longitude, const double heading, const geo_map& featureMap)
{
    pixel pDiver = convertGeoToPixelDouble(latitude, longitude, featureMap);

    int16_t diverTileX=0, diverTileY=0;
    pDiver = scalePixelForZoomedInTile(pDiver, diverTileX, diverTileY);

    if (_prevWaypointIndex != -1)
    {
      pixel p = convertGeoToPixelDouble(WraysburyWaypoints::waypoints[_prevWaypointIndex]._lat, WraysburyWaypoints::waypoints[_prevWaypointIndex]._long, featureMap);
      int16_t tileX=0,tileY=0;
      p = scalePixelForZoomedInTile(p,tileX,tileY);
      if (tileX == diverTileX && tileY == diverTileY)  // only show last target sprite on screen if tiles match
        _lastTargetSprite->pushToSprite(*_compositedScreenSprite, p.x-_mapAttr.featureSpriteRadius,p.y-_mapAttr.featureSpriteRadius,TFT_BLACK);
    }

    if (_targetWaypointIndex != -1)
    {
      pixel p = convertGeoToPixelDouble(WraysburyWaypoints::waypoints[_targetWaypointIndex]._lat, WraysburyWaypoints::waypoints[_targetWaypointIndex]._long, featureMap);
      int16_t tileX=0,tileY=0;
      p = scalePixelForZoomedInTile(p,tileX,tileY);
  
      if (tileX == diverTileX && tileY == diverTileY)  // only show target sprite on screen if tiles match
        _targetSprite->pushToSprite(*_compositedScreenSprite, p.x-_mapAttr.featureSpriteRadius,p.y-_mapAttr.featureSpriteRadius,TFT_BLACK);
    }

    // draw direction line to next target.
    if (_useDiverHeading)
    {
      _diverRotatedSprite->fillSprite(TFT_BLACK);
      _diverSprite->pushRotated(*_diverRotatedSprite,heading,TFT_BLACK); // BLACK is the transparent colour
      _diverRotatedSprite->pushToSprite(*_compositedScreenSprite,pDiver.x-_mapAttr.diverSpriteRadius,pDiver.y-_mapAttr.diverSpriteRadius,TFT_BLACK); // BLACK is the transparent colour
    }
    else
    {
      _diverPlainSprite->pushToSprite(*_compositedScreenSprite,pDiver.x-_mapAttr.diverSpriteRadius,pDiver.y-_mapAttr.diverSpriteRadius,TFT_BLACK); // BLACK is the transparent colour
    }
}

TFT_eSprite& MapScreen_ex::getCompositeSprite()
{
  return *_compositedScreenSprite;
}

TFT_eSprite& MapScreen_ex::getCleanMapSprite()
{
  return *_cleanMapAndFeaturesSprite;
}

void MapScreen_ex::writeOverlayTextToCompositeMapSprite()
{
  _compositedScreenSprite->setTextColor(TFT_WHITE);
  _compositedScreenSprite->setTextWrap(true);
  _compositedScreenSprite->setCursor(0,0);
  _compositedScreenSprite->println("_ex TEST STRING");
}

void MapScreen_ex::drawRegistrationPixelsOnCleanMapSprite(const geo_map& featureMap)
{
  for (int i=0; i < getRegistrationMarkLocationsSize(); i++)   // MBJ REFACTOR - needs range and enumerate from C++20 as index used.
  {
    pixel p = getRegistrationMarkLocation(i);         // index i used here

    int16_t tileX=0,tileY=0;
    p = scalePixelForZoomedInTile(p,tileX,tileY);
    if (tileX != _tileXToDisplay || tileY != _tileYToDisplay)
      continue;
  
  //    Serial.printf("%i,%i      s: %i,%i\n",p.x,p.y,sP.x,sP.y);
    if (p.x >= 0 && p.x < getTFTWidth() && p.y >=0 && p.y < getTFTHeight())   // CHANGE these to take account of tile shown  
    {
      if (_mapAttr.useSpriteForFeatures)
        _featureSprite->pushToSprite(*_cleanMapAndFeaturesSprite,p.x - _mapAttr.featureSpriteRadius, p.y - _mapAttr.featureSpriteRadius,TFT_BLACK);
      else
        _cleanMapAndFeaturesSprite->fillCircle(p.x,p.y,_mapAttr.featureSpriteRadius,p.colour);
        
  //      debugPixelFeatureOutput(WraysburyWaypoints::waypoints[i], p, featureMap);
    }
  }
}

void MapScreen_ex::drawFeaturesOnCleanMapSprite(const geo_map& featureMap)
{
  for(int i=0;i<WraysburyWaypoints::getWaypointsCount();i++)
  {
    pixel p = convertGeoToPixelDouble(WraysburyWaypoints::waypoints[i]._lat, WraysburyWaypoints::waypoints[i]._long, featureMap);

    int16_t tileX=0,tileY=0;
    p = scalePixelForZoomedInTile(p,tileX,tileY);

//    Serial.printf("%i,%i      s: %i,%i\n",p.x,p.y,sP.x,sP.y);
    if (tileX == _tileXToDisplay && tileY == _tileYToDisplay && p.x >= 0 && p.x < getTFTWidth() && p.y >=0 && p.y < getTFTHeight())   // CHANGE these to take account of tile shown  
    {
      if (_mapAttr.useSpriteForFeatures)
        _featureSprite->pushToSprite(*_cleanMapAndFeaturesSprite,p.x - _mapAttr.featureSpriteRadius, p.y - _mapAttr.featureSpriteRadius,TFT_BLACK);
      else
        _cleanMapAndFeaturesSprite->fillCircle(p.x,p.y,_mapAttr.featureSpriteRadius,_mapAttr.featureSpriteColour);
        
//      debugPixelFeatureOutput(WraysburyWaypoints::waypoints[i], p, featureMap);
    }
  }
}

MapScreen_ex::pixel MapScreen_ex::convertGeoToPixelDouble(double latitude, double longitude, const geo_map& mapToPlot) const
{  
  int16_t mapWidth = getTFTWidth(); // in pixels
  int16_t mapHeight = getTFTHeight(); // in pixels
  double mapLngLeft = mapToPlot.mapLongitudeLeft; // in degrees. the longitude of the left side of the map (i.e. the longitude of whatever is depicted on the left-most part of the map image)
  double mapLngRight = mapToPlot.mapLongitudeRight; // in degrees. the longitude of the right side of the map
  double mapLatBottom = mapToPlot.mapLatitudeBottom; // in degrees.  the latitude of the bottom of the map

  double mapLatBottomRad = mapLatBottom * PI / 180.0;
  double latitudeRad = latitude * PI / 180.0;
  double mapLngDelta = (mapLngRight - mapLngLeft);

  double worldMapWidth = ((mapWidth / mapLngDelta) * 360.0) / (2.0 * PI);
  double mapOffsetY = (worldMapWidth / 2.0 * log((1.0 + sin(mapLatBottomRad)) / (1.0 - sin(mapLatBottomRad))));

  int16_t x = (longitude - mapLngLeft) * ((double)mapWidth / mapLngDelta);
  int16_t y = (double)mapHeight - ((worldMapWidth / 2.0L * log((1.0 + sin(latitudeRad)) / (1.0 - sin(latitudeRad)))) - (double)mapOffsetY);

  return pixel(x,y);
}

void MapScreen_ex::debugScaledPixelForTile(pixel p, pixel pScaled, int16_t tileX,int16_t tileY) const
{
  Serial.printf("dspt x=%i y=%i --> x=%i y=%i  tx=%i ty=%i\n",p.x,p.y,pScaled.x,pScaled.y,tileX,tileY);
}

void MapScreen_ex::debugPixelMapOutput(const MapScreen_ex::pixel loc, const geo_map* thisMap, const geo_map& nextMap) const
{
  Serial.printf("dpmo %s %i, %i --> %s\n",thisMap->label,loc.x,loc.y,nextMap.label);
}

void MapScreen_ex::debugPixelFeatureOutput(const NavigationWaypoint& waypoint, MapScreen_ex::pixel loc, const geo_map& thisMap) const
{
  Serial.printf("dpfo x=%i y=%i %s %s \n",loc.x,loc.y,thisMap.label,waypoint._label);
}

void MapScreen_ex::drawFeaturesOnSpecifiedMapToScreen(int featureIndex, int16_t zoom, int16_t tileX, int16_t tileY)
{
  drawFeaturesOnSpecifiedMapToScreen(_maps[featureIndex],zoom,tileX,tileY);
}

void MapScreen_ex::drawFeaturesOnSpecifiedMapToScreen(const geo_map& featureAreaToShow, int16_t zoom, int16_t tileX, int16_t tileY)
{
    _currentMap = &featureAreaToShow;

    if (featureAreaToShow.mapData)
    {
      _cleanMapAndFeaturesSprite->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), zoom, tileX, tileY, 
                                                  featureAreaToShow.mapData, featureAreaToShow.swapBytes);
    }
    else
    {
      _cleanMapAndFeaturesSprite->fillSprite(featureAreaToShow.backColour);
    }
    
    drawFeaturesOnCleanMapSprite(featureAreaToShow);

    writeMapTitleToSprite(*_cleanMapAndFeaturesSprite, featureAreaToShow);

    copyFullScreenSpriteToDisplay(*_cleanMapAndFeaturesSprite);
}

void MapScreen_ex::testAnimatingDiverSpriteOnCurrentMap()
{
  const geo_map* featureAreaToShow = _currentMap;
  
  double latitude = featureAreaToShow->mapLatitudeBottom;
  double longitude = featureAreaToShow->mapLongitudeLeft;

  const int maxMoves = 20;
  for(int i=0;i<maxMoves;i++)
  {
    _cleanMapAndFeaturesSprite->pushToSprite(*_compositedScreenSprite,0,0);
    
    pixel p = convertGeoToPixelDouble(latitude, longitude, *featureAreaToShow);
    _diverSprite->pushToSprite(*_compositedScreenSprite,p.x-_mapAttr.diverSpriteRadius,p.y-_mapAttr.diverSpriteRadius,TFT_BLACK); // BLACK is the transparent colour

    copyFullScreenSpriteToDisplay(*_compositedScreenSprite);

    latitude+=0.0001;
    longitude+=0.0001;
  }
}

void MapScreen_ex::testDrawingMapsAndFeatures(uint8_t& currentMap, int16_t& zoom)
{  
  /*
  if (_m5->BtnA.isPressed())
  {
    zoom = (zoom == 2 ? 1 : 2);
    _m5->update();
    while (_m5->BtnA.isPressed())
    {
      _m5->update();
    }    
  }
  else if (_m5->BtnB.isPressed())
  {
    zoom = (zoom > 0 ? 0 : 1);
    _m5->update();
    while (_m5->BtnB.isPressed())
    {
      _m5->update();
    }    
  }

  if (zoom)
  {
    for (int tileY=0; tileY < zoom; tileY++)
    {
      for (int tileX=0; tileX < zoom; tileX++)
      {
        _m5->update();

        if (_m5->BtnA.isPressed())
        {
          zoom = (zoom == 2 ? 1 : 2);
          tileX=zoom;
          tileY=zoom;
          _m5->update();
          while (_m5->BtnA.isPressed())
          {
            _m5->update();
          }    
          break;
        }
        else if (_m5->BtnB.isPressed())
        {
          zoom = (zoom > 0 ? 0 : 1);
          tileX=zoom;
          tileY=zoom;
          _m5->update();
          while (_m5->BtnB.isPressed())
          {
            _m5->update();
          }    
          break;
        }

        const geo_map* featureAreaToShow = _maps+currentMap;        
        _cleanMapAndFeaturesSprite->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), zoom, tileX, tileY, featureAreaToShow->mapData);

//        drawFeaturesOnCleanMapSprite(featureAreaToShow, zoom, tileX, tileY);
    
        double latitude = featureAreaToShow->mapLatitudeBottom;
        double longitude = featureAreaToShow->mapLongitudeLeft;
      
        for(int i=0;i<20;i++)
        {
          _cleanMapAndFeaturesSprite->pushToSprite(*_compositedScreenSprite,0,0);
          
          pixel p = convertGeoToPixelDouble(latitude, longitude, featureAreaToShow);
          _diverSprite->pushToSprite(*_compositedScreenSprite,p.x-_mapAttr.diverSpriteRadius,p.y-_mapAttr.diverSpriteRadius,TFT_BLACK); // BLACK is the transparent colour
    
//          _compositedScreenSprite->pushSprite(0,0);
          _amoled->pushColors((uint16_t*)(_compositedScreenSprite->getPointer()),getTFTWidth()*getTFTHeight());
    
          latitude+=0.0001;
          longitude+=0.0001;
//          delay(50);
        }
      }
    }
  
    currentMap == 3 ? currentMap = 0 : currentMap++;
  }
  else
  {
    const geo_map* featureAreaToShow = _maps+4;
    const bool swapBytes = true;    // as original PNG is in opposite endian format (as not suitable for DMA)

    _cleanMapAndFeaturesSprite->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), 1, 0, 0, featureAreaToShow->mapData, swapBytes);

//    drawFeaturesOnCleanMapSprite(featureAreaToShow,1,0,0);

    double latitude = featureAreaToShow->mapLatitudeBottom;
    double longitude = featureAreaToShow->mapLongitudeLeft;
  
    for(int i=0;i<25;i++)
    {
      _cleanMapAndFeaturesSprite->pushToSprite(*_compositedScreenSprite,0,0);
      
      pixel p = convertGeoToPixelDouble(latitude, longitude, featureAreaToShow);
      _diverSprite->pushToSprite(*_compositedScreenSprite,p.x-_mapAttr.diverSpriteRadius,p.y-_mapAttr.diverSpriteRadius,TFT_BLACK); // BLACK is the transparent colour

      //_compositedScreenSprite->pushSprite(0,0);
      _amoled->pushColors((uint16_t*)(_compositedScreenSprite->getPointer()),getTFTWidth()*getTFTHeight());

      latitude+=0.0002;
      longitude+=0.0002;
    }
  } 
  */
}
