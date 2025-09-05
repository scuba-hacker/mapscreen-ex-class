#include "MapScreen_ex.h"
#include "TFT_eSPI.h"

#include <math.h>
#include <cstddef>
#include <memory>

#include "NavigationWaypoints.h"
#include "Traces.h"

#define USB_SERIAL Serial


MapScreen_ex::MapScreen_ex(TFT_eSPI& tft, const MapScreenAttr mapAttributes) : 
                                                        _zoom(1),
                                                        _prevZoom(1),
                                                        _tileXToDisplay(0),
                                                        _tileYToDisplay(0),
                                                        _showAllLake(false),
                                                        _lastDiverLatitude(0),
                                                        _lastDiverLongitude(0),
                                                        _lastDiverHeading(0),
                                                        _useDiverHeading(true),
                                                        _targetWaypointIndex(0),
                                                        _prevWaypointIndex(0),
                                                        _drawAllFeatures(true),
                                                        _tft(tft),
                                                        _mapAttr(mapAttributes),
                                                        _exitWaypointCount(-1),
                                                        _nextCrumbIndex(0),
                                                        _showBreadCrumbTrail(true),
                                                        _recordBreadCrumbTrail(false),
                                                        _recordActionCallback(nullptr),
                                                        LOG_HOOK(&Serial)
{
  _debugString[0] = '\0';

  _currentMap = nullptr;

  _baseMapCacheSprite = std::make_unique<TFT_eSprite>(&_tft);
  _compositedScreenSprite = std::make_shared<TFT_eSprite>(&_tft);
  _diverSprite = std::make_unique<TFT_eSprite>(&_tft);
  _diverPlainSprite = std::make_unique<TFT_eSprite>(&_tft);
  _diverRotatedSprite = std::make_unique<TFT_eSprite>(&_tft);
  _featureSprite = std::make_unique<TFT_eSprite>(&_tft);

  _targetSprite = std::make_unique<TFT_eSprite>(&_tft);
  _lastTargetSprite = std::make_unique<TFT_eSprite>(&_tft);
  _breadCrumbSprite = std::make_unique<TFT_eSprite>(&_tft);
  _rotatedBreadCrumbSprite = std::make_unique<TFT_eSprite>(&_tft);

  _pinSprite = std::make_unique<TFT_eSprite>(&_tft);
}

void MapScreen_ex::initMapScreen()
{
  initMaps();
  initSprites();
  initExitWaypoints();
  initFeatureColours();
}

void MapScreen_ex::initFeatureColours()
{
  waypointColourLookup[BLUE_BUOY] = TFT_BLUE;
  waypointColourLookup[NO_BUOY] = TFT_MAGENTA;
  waypointColourLookup[PLATFORM] = TFT_WHITE;
  waypointColourLookup[CONTAINER] = TFT_BLACK;
  waypointColourLookup[ORANGE_BUOY] = TFT_ORANGE;
  waypointColourLookup[JETTY] = TFT_GREEN;
  waypointColourLookup[UNMARKED] = TFT_GOLD;
  waypointColourLookup[UNKNOWN] = TFT_BROWN;
}


void MapScreen_ex::initFirstAndEndWaypointsIndices()
{
  _firstWaypointIndex = WraysburyWaypoints::getStartIndexWraysbury();  // default for wraysbury
  _endWaypointsIndex = WraysburyWaypoints::getEndWaypointIndexWraysbury();
}

void MapScreen_ex::displayMapLegend()
{
    int backColour = TFT_BLACK;

    _compositedScreenSprite->fillSprite(backColour);
    _compositedScreenSprite->setTextColor(TFT_CYAN);
    _compositedScreenSprite->drawCentreString("FEATURE LEGEND",getTFTWidth() / 2, 30,1);
    _compositedScreenSprite->setTextColor(TFT_WHITE);

    pixel anchor(100,130);

    int xOffsetLabel = 80;
    int yOffsetLabel = 5;

    int yRowOffset = 50;

    int featureRadius = 20;

    for (eWaypointCategory i=BLUE_BUOY; i <= UNKNOWN; i = (eWaypointCategory)((int)(i) + 1))
    {
      int colour = waypointColourLookup[i];

      if (colour == backColour)
        _compositedScreenSprite->drawCircle(anchor.x,anchor.y,featureRadius,~backColour);
      else
        _compositedScreenSprite->fillCircle(anchor.x,anchor.y,featureRadius,waypointColourLookup[i]);

        _compositedScreenSprite->drawString(featureCategoryToString(reinterpret_cast<eWaypointCategory>(i)), anchor.x + xOffsetLabel,anchor.y - featureRadius + yOffsetLabel);
      anchor.y += yRowOffset;
    }
    copyCompositeSpriteToDisplay();

    delay(2000);
}

void MapScreen_ex::initSprites()
{
  _baseMap = (useBaseMapCache() ? _baseMapCacheSprite : _compositedScreenSprite);

  if (useBaseMapCache())
  {
    _baseMapCacheSprite->setColorDepth(16);
    _baseMapCacheSprite->createSprite(getTFTWidth(),getTFTHeight());
  }

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

  _breadCrumbSprite->setColorDepth(16);
  _breadCrumbSprite->createSprite(_mapAttr.breadCrumbWidth, _mapAttr.breadCrumbWidth);
  _breadCrumbSprite->fillTriangle((_mapAttr.breadCrumbWidth - 1) / 2 + 1,0,6,_mapAttr.breadCrumbWidth,15,_mapAttr.breadCrumbWidth,_mapAttr.breadCrumbColour);

  _rotatedBreadCrumbSprite->setColorDepth(16);
  _rotatedBreadCrumbSprite->createSprite(_mapAttr.breadCrumbWidth,_mapAttr.breadCrumbWidth);

  _pinSprite->setColorDepth(16);
  _pinSprite->createSprite(_mapAttr.pinWidth,_mapAttr.pinWidth);  
  _pinSprite->fillRoundRect(0,0,_mapAttr.pinWidth, _mapAttr.pinWidth, 5, _mapAttr.pinBackColour);
  _pinSprite->fillCircle(_mapAttr.pinWidth/2,_mapAttr.pinWidth/2,_mapAttr.pinWidth/3, _mapAttr.pinForeColour);
}

void MapScreen_ex::initExitWaypoints()
{
  int currentExitIndex=-1;
  
  for (int i=_firstWaypointIndex; i<_endWaypointsIndex; i++)
//  for (int i=0; i<WraysburyWaypoints::getWaypointsCount(); i++)
  {
    if (strncmp(WraysburyWaypoints::waypoints[i]._label, "Z0", 2) == 0)
    {
      _exitWaypointIndices[++currentExitIndex] = i;
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
  for (uint8_t i = getFirstDetailMapIndex(); i<getEndDetailMaps(); i++)
  {
    p = convertGeoToPixelDouble(diverLatitude, diverLongitude, _maps[i]);

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
  _prevZoom = _zoom = 1;
  _tileXToDisplay = _tileXToDisplay = 0;
  fillScreen(TFT_BLACK);
}

void MapScreen_ex::setTargetWaypointByLabel(const char* label)
{
  _prevWaypointIndex = _targetWaypointIndex;
  _targetWaypointIndex = -1;
  const int numberCharsToCompare = 3;
  // find targetWayPoint in the NavigationWaypoints array by first 3 chars
  for (int i=_firstWaypointIndex; i < _endWaypointsIndex; i++)
  {
    if (strncmp(WraysburyWaypoints::waypoints[i]._label, label, numberCharsToCompare) == 0)
    {
      _targetWaypointIndex=i;
      break;
    }
  }
}

void MapScreen_ex::setZoom(const int16_t zoom)
{
  _prevZoom = _zoom;
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
  _prevZoom = _zoom;

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

int MapScreen_ex::getClosestJettyIndex(double& shortestDistance)
{
  shortestDistance = 1e10;
  int closestExitWaypointIndex = 0;

  //sprintf(_debugString,"getclosestsjetty.."); fillScreen(TFT_BROWN); delay(1000);
  for (int i=0; i<_exitWaypointCount; i++)
  {
    double distance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude, WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._lat, WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._long);
  
    if (distance < shortestDistance)
    {
      shortestDistance =  distance;
      closestExitWaypointIndex = i;
    }
  }

  //sprintf(_debugString,"closestjetty index = %i\njetty count=%i\njettywaypoint=%i", closestExitWaypointIndex,_exitWaypointCount,_exitWaypointIndices[closestExitWaypointIndex]); fillScreen(TFT_BROWN); delay(1000);
      
  return _exitWaypointIndices[closestExitWaypointIndex];
}

int MapScreen_ex::getClosestFeatureIndex(double& shortestDistance)
{
  shortestDistance = 1e99;
  int closestFeatureIndex = 255;

  for (int i=_firstWaypointIndex; i < _endWaypointsIndex; i++)
  {
    double distance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude, WraysburyWaypoints::waypoints[i]._lat, WraysburyWaypoints::waypoints[i]._long);
  
    if (distance < shortestDistance)
    {
      shortestDistance =  distance;
      closestFeatureIndex = i;
    }
  }
      
  return closestFeatureIndex;
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

  //sprintf(_debugString,"Done getNextMapByPixelLocation"); fillScreen(TFT_BLACK); delay(1000);

  if (nextMap != _currentMap)
  {
      p = convertGeoToPixelDouble(diverLatitude, diverLongitude, *nextMap);
      //sprintf(_debugString,"convert geo to pixel %i, %i",p.x,p.y); fillScreen(TFT_BLACK); delay(1000);
  }

  int16_t prevTileX = _tileXToDisplay;
  int16_t prevTileY = _tileYToDisplay;
  
  //sprintf(_debugString,"Do scale..."); fillScreen(TFT_BLACK); delay(1000);
  p = scalePixelForZoomedInTile(p,_tileXToDisplay,_tileYToDisplay);
  //sprintf(_debugString,"scale for zoom %i, %i",p.x,p.y); fillScreen(TFT_GREEN); delay(1000);

  if (_prevZoom != _zoom)
  {
    forceFirstMapDraw = true;
    _prevZoom = _zoom;
  }

  if (!useBaseMapCache() || nextMap != _currentMap || prevTileX != _tileXToDisplay || prevTileY != _tileYToDisplay || forceFirstMapDraw)
  {
    if (nextMap->mapData)
    {
      //sprintf(_debugString,"pushImageScaled %i, %i, %i",_zoom,_tileXToDisplay,_tileYToDisplay); fillScreen(TFT_GREEN); delay(2000);
    
      _baseMap->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), _zoom, _tileXToDisplay, _tileYToDisplay, 
                                                  nextMap->mapData, nextMap->swapBytes);

      if (_drawAllFeatures)
      {
        //sprintf(_debugString,"drawFeaturesBase"); fillScreen(TFT_RED); delay(1000);
        drawFeaturesOnBaseMapSprite(*nextMap, *_baseMap);
//        drawRegistrationPixelsOnCleanMapSprite(*nextMap);    // Test Pattern
      }
      
        //sprintf(_debugString,"drawMapScaleToSprite"); fillScreen(TFT_BLUE); delay(1000);
      drawMapScaleToSprite(*_baseMap, *nextMap);
    }
    else
    {
      //sprintf(_debugString,"base fill",_zoom,_tileXToDisplay,_tileYToDisplay); fillScreen(TFT_RED); delay(2000);
      _baseMap->fillSprite(nextMap->backColour);
      //sprintf(_debugString,"base draw feat"); fillScreen(TFT_BLUE); delay(2000);
      drawFeaturesOnBaseMapSprite(*nextMap, *_baseMap);  // need to revert zoom to 1
    }
  }

  ////sprintf(_debugString,"baseMapCacheSprite push to comp"); fillScreen(TFT_GREEN); delay(1000);

  _baseMapCacheSprite->pushToSprite(*_compositedScreenSprite,0,0);

  ////sprintf(_debugString,"drawTracesOnComp"); fillScreen(TFT_BROWN); delay(1000);

  drawTracesOnCompositeMapSprite(diverLatitude, diverLongitude, *nextMap);

  ////sprintf(_debugString,"drawBread"); fillScreen(TFT_BLUE); delay(1000);
  drawBreadCrumbTrailOnCompositeMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);

  ////sprintf(_debugString,"drawPins"); fillScreen(TFT_GREEN); delay(1000);
  drawPlacedPins(diverLatitude, diverLongitude, *nextMap);

  //sprintf(_debugString,"drawHeading"); fillScreen(TFT_BLUE); delay(1000);
  drawHeadingLineOnCompositeMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);

  _nearestExitBearing = drawDirectionalLineOnCompositeSprite(diverLatitude, diverLongitude, *nextMap,getClosestJettyIndex(_distanceToNearestExit), _mapAttr.nearestExitLineColour, _mapAttr.nearestExitLinePixelLength);
  //sprintf(_debugString,"exit bearing: %f\nexit distance: %f", _nearestExitBearing, _distanceToNearestExit); fillScreen(TFT_GREEN); delay(1000);

  //sprintf(_debugString,"drawDirLineT"); fillScreen(TFT_GREEN); delay(1000);
  _targetBearing = drawDirectionalLineOnCompositeSprite(diverLatitude, diverLongitude, *nextMap,_targetWaypointIndex, _mapAttr.targetLineColour, _mapAttr.targetLinePixelLength);

  //sprintf(_debugString,"distBet"); fillScreen(TFT_GREEN); delay(1000);
  _targetDistance = distanceBetween(diverLatitude, diverLongitude, WraysburyWaypoints::waypoints[_targetWaypointIndex]._lat, WraysburyWaypoints::waypoints[_targetWaypointIndex]._long);

  //sprintf(_debugString,"closeFeat"); fillScreen(TFT_GREEN); delay(1000);
  _nearestFeatureIndex = getClosestFeatureIndex(_nearestFeatureDistance);

  //sprintf(_debugString,"distBet2"); fillScreen(TFT_GREEN); delay(1000);
  _nearestFeatureDistance = distanceBetween(diverLatitude, diverLongitude, WraysburyWaypoints::waypoints[_nearestFeatureIndex]._lat, WraysburyWaypoints::waypoints[_nearestFeatureIndex]._long);

  //sprintf(_debugString,"degcourseto"); fillScreen(TFT_GREEN); delay(1000);
  _nearestFeatureBearing = degreesCourseTo(diverLatitude, diverLongitude, WraysburyWaypoints::waypoints[_nearestFeatureIndex]._lat, WraysburyWaypoints::waypoints[_nearestFeatureIndex]._long);

  //sprintf(_debugString,"writetitle"); fillScreen(TFT_GREEN); delay(1000);
  writeMapTitleToSprite(*_compositedScreenSprite, *nextMap);
      
  //sprintf(_debugString,"diverdraw"); fillScreen(TFT_GREEN); delay(1000);
  drawDiverOnCompositedMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);
  
  //sprintf(_debugString,"copyfulltodisp"); fillScreen(TFT_GREEN); delay(1000);
  copyFullScreenSpriteToDisplay(*_compositedScreenSprite);

  _currentMap = nextMap;
}

bool MapScreen_ex::isPixelOutsideScreenExtent(const MapScreen_ex::pixel loc) const
{
  return (loc.x < 0 || loc.x >= getTFTWidth() || loc.y <0 || loc.y >= getTFTHeight()); 
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

int MapScreen_ex::drawDirectionalLineOnCompositeSprite(const double diverLatitude, const double diverLongitude, 
                                                  const geo_map& featureMap, const int waypointIndex, uint16_t colour, int indicatorLength)
{
  int heading = 0;

  //sprintf(_debugString,"1"); fillScreen(TFT_GREEN); delay(1000);

  const NavigationWaypoint& w = WraysburyWaypoints::waypoints[waypointIndex];

  //sprintf(_debugString,"2"); fillScreen(TFT_GREEN); delay(1000);

  pixel pDiver = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  //sprintf(_debugString,"3"); fillScreen(TFT_GREEN); delay(1000);
  int16_t diverTileX=0,diverTileY=0;
  pDiver = scalePixelForZoomedInTile(pDiver,diverTileX,diverTileY);

  //sprintf(_debugString,"4"); fillScreen(TFT_GREEN); delay(1000);
  int16_t targetTileX=0,targetTileY=0;
  pixel pTarget = convertGeoToPixelDouble(w._lat, w._long, featureMap);

  //sprintf(_debugString,"5"); fillScreen(TFT_GREEN); delay(1000);
  if (!isPixelOutsideScreenExtent(convertGeoToPixelDouble(w._lat, w._long, featureMap)))
  {
  //sprintf(_debugString,"6"); fillScreen(TFT_GREEN); delay(1000);
    // use line between diver and target locations
    pTarget.x = pTarget.x * _zoom - getTFTWidth() * diverTileX;
    pTarget.y = pTarget.y * _zoom - getTFTHeight() * diverTileY;

  //sprintf(_debugString,"7"); fillScreen(TFT_GREEN); delay(1000);
    _compositedScreenSprite->drawLine(pDiver.x, pDiver.y, pTarget.x,pTarget.y,colour);

    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y-2, pTarget.x,pTarget.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y+2, pTarget.x,pTarget.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y-2, pTarget.x,pTarget.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y+2, pTarget.x,pTarget.y,colour);

  //sprintf(_debugString,"8"); fillScreen(TFT_GREEN); delay(1000);
    if (pTarget.y < pDiver.y)
      heading = (int)(atan((double)(pTarget.x - pDiver.x) / (double)(-(pTarget.y - pDiver.y))) * 180.0 / PI) % 360;
    else if (pTarget.y > pDiver.y)
      heading = (int)(180.0 + atan((double)(pTarget.x - pDiver.x) / (double)(-(pTarget.y - pDiver.y))) * 180.0 / PI);
  //sprintf(_debugString,"9"); fillScreen(TFT_GREEN); delay(1000);
  }
  else
  {
  //sprintf(_debugString,"10"); fillScreen(TFT_GREEN); delay(1000);
    heading = degreesCourseTo(diverLatitude,diverLongitude,w._lat,w._long);

    // use lat/long to draw outside map area with arbitrary length.
    pixel pHeading;
  
  //sprintf(_debugString,"11"); fillScreen(TFT_GREEN); delay(1000);
    double rads = heading * PI / 180.0;  
    pHeading.x = pDiver.x + indicatorLength * sin(rads);
    pHeading.y = pDiver.y - indicatorLength * cos(rads);

  //sprintf(_debugString,"12"); fillScreen(TFT_GREEN); delay(1000);
    _compositedScreenSprite->drawLine(pDiver.x, pDiver.y, pHeading.x,pHeading.y,colour);
  
    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y-2, pHeading.x,pHeading.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x-2, pDiver.y+2, pHeading.x,pHeading.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y-2, pHeading.x,pHeading.y,colour);
    _compositedScreenSprite->drawLine(pDiver.x+2, pDiver.y+2, pHeading.x,pHeading.y,colour);
  //sprintf(_debugString,"13"); fillScreen(TFT_GREEN); delay(1000);
  }
  //sprintf(_debugString,"14"); fillScreen(TFT_GREEN); delay(1000);

  return heading;
}

void MapScreen_ex::toggleShowBreadCrumbTrail()
{
  _showBreadCrumbTrail = !_showBreadCrumbTrail;

  if (!_showBreadCrumbTrail && _recordBreadCrumbTrail)
    toggleRecordBreadCrumbTrail();
}

void MapScreen_ex::toggleRecordBreadCrumbTrail()
{
  _recordBreadCrumbTrail = !_recordBreadCrumbTrail;

  if (_recordBreadCrumbTrail)
  {
    _showBreadCrumbTrail=true;
    _breadCrumbCountDown = _mapAttr.breadCrumbDropFixCount;
  }

  if (_recordActionCallback)
    _recordActionCallback(_recordBreadCrumbTrail);
}

void MapScreen_ex::setBreadCrumbTrailRecord(const bool enable)
{
  if (_recordBreadCrumbTrail != enable)
  {
    toggleRecordBreadCrumbTrail();
  }
}

void MapScreen_ex::clearBreadCrumbTrail()
{
  _nextCrumbIndex = 0;
  _breadCrumbCountDown = _mapAttr.breadCrumbDropFixCount;
  _recordBreadCrumbTrail = true; // force toggle to disable recordbreadcrumb and publish message to mako regardless.
  toggleRecordBreadCrumbTrail();
}

void MapScreen_ex::placePin(const double lat, const double lng, const double head, const double dep)
{
  _placedPins[_placedPinIndex++] = BreadCrumb(lat,lng,head,dep);
}

void MapScreen_ex::drawPlacedPins(const double diverLatitude, const double diverLongitude, const geo_map& featureMap)
{                                  
  int16_t diverTileX=0,diverTileY=0;
  pixel diverLocation = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  diverLocation = scalePixelForZoomedInTile(diverLocation,diverTileX,diverTileY);

  // draw the entire array of pins to composite sprite within map view
  for (int i=0; i < _placedPinIndex; i++)
  {
    pixel pinLocation = convertGeoToPixelDouble(_placedPins[i]._lat, _placedPins[i]._long, featureMap);
    if (isPixelOutsideScreenExtent(pinLocation))
      continue;

    int16_t pinTileX=0,pinTileY=0;
    pinLocation = scalePixelForZoomedInTile(pinLocation,pinTileX,pinTileY);

    if (pinTileX != diverTileX || pinTileY != diverTileY)
      continue;

    _pinSprite->pushToSprite(*_compositedScreenSprite,pinLocation.x-_mapAttr.pinWidth/2,pinLocation.y-_mapAttr.pinWidth/2,TFT_BLACK); // BLACK is the transparent colour
  }
}

void MapScreen_ex::drawTracesOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, const geo_map& featureMap)
{
  int16_t diverTileX=0,diverTileY=0;
  pixel diverLocation = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  diverLocation = scalePixelForZoomedInTile(diverLocation,diverTileX,diverTileY);

  int n = WraysburyTraces::getAllTraceCount();

  for (int i=0; i < n; i++)
  {
    pixel pointLocation = convertGeoToPixelDouble(WraysburyTraces::all_trace[i]._la, WraysburyTraces::all_trace[i]._lo, featureMap);
    if (isPixelOutsideScreenExtent(pointLocation))
      continue;

    int16_t pointTileX=0,pointTileY=0;
    pointLocation = scalePixelForZoomedInTile(pointLocation,pointTileX,pointTileY);

    if (pointTileX != diverTileX || pointTileY != diverTileY)
      continue;

    _compositedScreenSprite->drawRect(pointLocation.x-1,pointLocation.y-1,_mapAttr.tracePointSize,_mapAttr.tracePointSize,_mapAttr.traceColour);
  }
}

void MapScreen_ex::drawBreadCrumbTrailOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, 
                                                            const double heading, const geo_map& featureMap)
{
  if (_recordBreadCrumbTrail)
  {
    _breadCrumbCountDown--;

    if (_nextCrumbIndex < _maxBreadCrumbs && _breadCrumbCountDown == 0)
    {
      _breadCrumbTrail[_nextCrumbIndex++] = BreadCrumb(diverLatitude, diverLongitude, heading);
      _breadCrumbCountDown = _mapAttr.breadCrumbDropFixCount;
    }

    if (_breadCrumbCountDown % 2)        // blink the record light
    {
      const int recordIndicatorWidth = 30;
      _compositedScreenSprite->fillRect(0,getTFTHeight()-recordIndicatorWidth-1,recordIndicatorWidth,recordIndicatorWidth,TFT_RED);
    }
  }

  if (_showBreadCrumbTrail)
  {
    int16_t diverTileX=0,diverTileY=0;
    pixel diverLocation = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
    diverLocation = scalePixelForZoomedInTile(diverLocation,diverTileX,diverTileY);

  // draw the entire array of pins to composite sprite within map view
    for (int i=0; i < _nextCrumbIndex; i++)
    {
      pixel crumbLocation = convertGeoToPixelDouble(_breadCrumbTrail[i]._lat, _breadCrumbTrail[i]._long, featureMap);
      if (isPixelOutsideScreenExtent(crumbLocation))
        continue;

      int16_t crumbTileX=0,crumbTileY=0;
      crumbLocation = scalePixelForZoomedInTile(crumbLocation,crumbTileX,crumbTileY);

      if (crumbTileX != diverTileX || crumbTileY != diverTileY)
        continue;
  
      _rotatedBreadCrumbSprite->fillSprite(TFT_BLACK);
      _breadCrumbSprite->pushRotated(*_rotatedBreadCrumbSprite,_breadCrumbTrail[i]._heading,TFT_BLACK); // BLACK is the transparent colour
      _rotatedBreadCrumbSprite->pushToSprite(*_compositedScreenSprite,crumbLocation.x-_mapAttr.breadCrumbWidth/2,crumbLocation.y-_mapAttr.breadCrumbWidth/2,TFT_BLACK); // BLACK is the transparent colour
    }
  }
}

void MapScreen_ex::drawHeadingLineOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, 
                                                            const double heading, const geo_map& featureMap)
{
  int16_t tileX=0,tileY=0;
  pixel pDiver = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  pDiver = scalePixelForZoomedInTile(pDiver,tileX,tileY);
  
//  const double hY_t3potoneuse=50;
  pixel pHeading;

  double rads = heading * PI / 180.0;  
  pHeading.x = pDiver.x + _mapAttr.diverHeadingLinePixelLength * sin(rads);
  pHeading.y = pDiver.y - _mapAttr.diverHeadingLinePixelLength * cos(rads);

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

TFT_eSprite& MapScreen_ex::getBaseMapSprite()
{
  return *_baseMap;
}

void MapScreen_ex::writeOverlayTextToCompositeMapSprite()
{
  _compositedScreenSprite->setTextColor(TFT_WHITE);
  _compositedScreenSprite->setTextWrap(true);
  _compositedScreenSprite->setCursor(0,0);
  _compositedScreenSprite->println("_ex TEST STRING");
}

void MapScreen_ex::drawRegistrationPixelsOnBaseMapSprite(const geo_map& featureMap)
{
  for (int i=0; i < getRegistrationMarkLocationsSize(); i++)
  {
    pixel p = getRegistrationMarkLocation(i);

    int16_t tileX=0,tileY=0;
    p = scalePixelForZoomedInTile(p,tileX,tileY);
    if (tileX != _tileXToDisplay || tileY != _tileYToDisplay)
      continue;
  
  //    Serial.printf("%i,%i      s: %i,%i\n",p.x,p.y,sP.x,sP.y);
    if (p.x >= 0 && p.x < getTFTWidth() && p.y >=0 && p.y < getTFTHeight())   // CHANGE these to take account of tile shown  
    {
      if (_mapAttr.useSpriteForFeatures)
        _featureSprite->pushToSprite(*_baseMap,p.x - _mapAttr.featureSpriteRadius, p.y - _mapAttr.featureSpriteRadius,TFT_BLACK);
      else
        _baseMap->fillCircle(p.x,p.y,_mapAttr.featureSpriteRadius,p.colour);
        
  //      debugPixelFeatureOutput(WraysburyWaypoints::waypoints[i], p, featureMap);
    }
  }
}

void MapScreen_ex::drawFeaturesOnBaseMapSprite(const geo_map& featureMap, TFT_eSprite& sprite)
{
  for(int i=_firstWaypointIndex;i<_endWaypointsIndex;i++)
  {
    pixel p = convertGeoToPixelDouble(WraysburyWaypoints::waypoints[i]._lat, WraysburyWaypoints::waypoints[i]._long, featureMap);

    int16_t tileX=0,tileY=0;
    p = scalePixelForZoomedInTile(p,tileX,tileY);

//    Serial.printf("%i,%i      s: %i,%i\n",p.x,p.y,sP.x,sP.y);
    if (tileX == _tileXToDisplay && tileY == _tileYToDisplay && p.x >= 0 && p.x < getTFTWidth() && p.y >=0 && p.y < getTFTHeight())   // CHANGE these to take account of tile shown  
    {
      if (_mapAttr.useSpriteForFeatures)
      {
        _featureSprite->pushToSprite(&sprite,p.x - _mapAttr.featureSpriteRadius, p.y - _mapAttr.featureSpriteRadius,TFT_BLACK);
      }
      else
      {
        sprite.fillCircle(p.x,p.y,_mapAttr.featureSpriteRadius,waypointColourLookup[WraysburyWaypoints::waypoints[i]._cat]);
      }
        
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
      _baseMap->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), zoom, tileX, tileY, 
                                                  featureAreaToShow.mapData, featureAreaToShow.swapBytes);
    }
    else
    {
      _baseMap->fillSprite(featureAreaToShow.backColour);
    }
    
    drawFeaturesOnBaseMapSprite(featureAreaToShow,*_baseMap);

    writeMapTitleToSprite(*_baseMap, featureAreaToShow);

    copyFullScreenSpriteToDisplay(*_baseMap);
}

void MapScreen_ex::testAnimatingDiverSpriteOnCurrentMap()
{
  const geo_map* featureAreaToShow = _currentMap;
  
  double latitude = featureAreaToShow->mapLatitudeBottom;
  double longitude = featureAreaToShow->mapLongitudeLeft;

  const int maxMoves = 20;
  for(int i=0;i<maxMoves;i++)
  {
    _baseMapCacheSprite->pushToSprite(*_compositedScreenSprite,0,0);
    
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
