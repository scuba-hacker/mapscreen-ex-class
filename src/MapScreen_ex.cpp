#include "MapScreen_ex.h"
#include "TFT_eSPI.h"

#include <math.h>
#include <cstddef>
#include <memory>
#include <algorithm>

#include "NavigationWaypoints.h"
#include "Traces.h"

#include "TinyGPS++.h"

#include <LittleFS.h>
#include <FS.h>

#include <PNGdec.h>
PNG png;

#define USB_SERIAL Serial

// PNG callback functions for LittleFS - based on PNGDisplay.inl implementation
static fs::File pngFile;
static TFT_eSprite* pngTargetSprite = nullptr;
static std::vector<uint16_t> pngPixelBuffer;  // Static buffer for PNG decoding (screen-sized, reused for each decode)
static std::string lastLoadedPngFilename;      // Tracks which PNG is currently decoded in pngPixelBuffer

// Pre-computed trace pixel cache — geo->pixel is expensive (float math), so bake once per map change
static std::vector<MapScreen_ex::pixel> tracePixelCache;
static const MapScreen_ex::geo_map* tracePixelCacheMap = nullptr;

static void * pngOpenLFS(const char *filename, int32_t *size) {
  pngFile = LittleFS.open(filename, FILE_READ);
  if (pngFile) {
      *size = pngFile.size();
      return &pngFile;
  } else {
      return NULL;
  }
}

static void pngClose(void *handle) {
  if (pngFile) pngFile.close();
}

static int32_t pngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!pngFile) return 0;
  return pngFile.read(buffer, length);
}

static int32_t pngSeek(PNGFILE *handle, int32_t position) {
  if (!pngFile) return 0;
  return pngFile.seek(position);
}

static int pngDrawToSprite(PNGDRAW *pDraw) {
  if (pngPixelBuffer.empty()) return 0;
  
  uint16_t usPixels[pDraw->iWidth];
  png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  
  // Write line y to pixel buffer
  size_t offset = (size_t)pDraw->y * pDraw->iWidth;
  if (offset + pDraw->iWidth <= pngPixelBuffer.size()) {
    memcpy(&pngPixelBuffer[offset], usPixels, pDraw->iWidth * sizeof(uint16_t));
  }
  
  return 1;
}


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
                                                        _exitWaypointCount(0),
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

    // Allocate PNG pixel buffer only when base cache is enabled (screen-sized, reused for each PNG decode)
    pngPixelBuffer.resize(getTFTWidth() * getTFTHeight());
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
  {
    // starting at the first waypoint for the map, find the first waypoint with code prefix Z0
    // this is the first exit waypoint index, continue search to find out how many codes with
    // this prefix are before the end of the waypoints list (for this location).
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

void MapScreen_ex::clearMap(const bool clearToBlack)
{
  _currentMap = nullptr;
  _prevZoom = _zoom = 1;
  _tileXToDisplay = _tileXToDisplay = 0;
  if (clearToBlack)
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
  USB_SERIAL.printf("switch to zoom %hu normal map\n",zoom);
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
    USB_SERIAL.println("setAllLakeShown(true): switch to zoom 1 all lake map\n");
  }
  else
  {
    _showAllLake = false;
    _zoom = 1;
    _currentMap=nullptr;      // force recalculate of currentmap
    USB_SERIAL.println("setAllLakeShown(false): switch to zoom 1 normal map\n");
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
    USB_SERIAL.println("switch to zoom 1 normal map\n");
  }
  else if (!_showAllLake && _zoom == 4)
  {
    _showAllLake = true;
    _zoom = 1;
    _currentMap = _maps+getAllMapIndex();
    USB_SERIAL.println("switch to zoom 1 ALL map\n");
  }
  else if (!_showAllLake && _zoom == 3)
  {
    _showAllLake = false;
    _zoom = 4;
    USB_SERIAL.println("switch to zoom 4 normal map\n");
  }
  else if (!_showAllLake && _zoom == 2)
  {
    _showAllLake = false;
    _zoom = 3;
    USB_SERIAL.println("switch to zoom 3 normal map\n");
  }
  else if (!_showAllLake && _zoom == 1)
  {
    _showAllLake = false;
    _zoom = 2;
    USB_SERIAL.println("switch to zoom 2 normal map\n");
  }
}

int MapScreen_ex::getClosestJettyIndex(double& shortestDistance, bool useFastApprox)
{
  int closestExitWaypointIndex = 0;

  if (useFastApprox)
  {
    // Equirectangular squared distance — one cos() then pure arithmetic per waypoint.
    // Ordering identical to Haversine at dive-site scale; difference < GPS noise (3m).
    const double clat = cos(_lastDiverLatitude * DEG_TO_RAD);
    double shortestD2 = 1e10;
    for (int i=0; i<_exitWaypointCount; i++)
    {
      const double dlat = WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._lat - _lastDiverLatitude;
      const double dlon = (WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._long - _lastDiverLongitude) * clat;
      const double d2 = dlat*dlat + dlon*dlon;
      if (d2 < shortestD2) { shortestD2 = d2; closestExitWaypointIndex = i; }
    }
  }
  else
  {
    shortestDistance = 1e10;
    for (int i=0; i<_exitWaypointCount; i++)
    {
      const double distance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude, WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._lat, WraysburyWaypoints::waypoints[_exitWaypointIndices[i]]._long);
      if (distance < shortestDistance) { shortestDistance = distance; closestExitWaypointIndex = i; }
    }
  }

  // Always compute actual metres for the winner — used by the UI
  shortestDistance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude,
    WraysburyWaypoints::waypoints[_exitWaypointIndices[closestExitWaypointIndex]]._lat,
    WraysburyWaypoints::waypoints[_exitWaypointIndices[closestExitWaypointIndex]]._long);

  return _exitWaypointIndices[closestExitWaypointIndex];
}

int MapScreen_ex::getClosestFeatureIndex(double& shortestDistance, bool useFastApprox)
{
  int closestFeatureIndex = _firstWaypointIndex;

  if (useFastApprox)
  {
    // Equirectangular squared distance — one cos() then pure arithmetic per waypoint.
    const double clat = cos(_lastDiverLatitude * DEG_TO_RAD);
    double shortestD2 = 1e99;
    for (int i=_firstWaypointIndex; i < _endWaypointsIndex; i++)
    {
      const double dlat = WraysburyWaypoints::waypoints[i]._lat - _lastDiverLatitude;
      const double dlon = (WraysburyWaypoints::waypoints[i]._long - _lastDiverLongitude) * clat;
      const double d2 = dlat*dlat + dlon*dlon;
      if (d2 < shortestD2) { shortestD2 = d2; closestFeatureIndex = i; }
    }
  }
  else
  {
    shortestDistance = 1e99;
    for (int i=_firstWaypointIndex; i < _endWaypointsIndex; i++)
    {
      const double distance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude, WraysburyWaypoints::waypoints[i]._lat, WraysburyWaypoints::waypoints[i]._long);
      if (distance < shortestDistance) { shortestDistance = distance; closestFeatureIndex = i; }
    }
  }

  // Always compute actual metres for the winner — used by the UI
  shortestDistance = distanceBetween(_lastDiverLatitude, _lastDiverLongitude,
    WraysburyWaypoints::waypoints[closestFeatureIndex]._lat,
    WraysburyWaypoints::waypoints[closestFeatureIndex]._long);

  return closestFeatureIndex;
}

void MapScreen_ex::drawPNG(const char* filename, bool swapBytes)
{
  // Automatic PNG loading for map rendering
  if (!filename || !useBaseMapCache() || pngPixelBuffer.empty()) {
      return;
  }

  // Skip decode if this PNG is already in the buffer — zoom/tile changes reuse the existing decode
  if (lastLoadedPngFilename == filename) {
      USB_SERIAL.printf("  → PNG cache hit, reusing buffer: %s\n", filename);
      return;
  }

  if (!LittleFS.exists(filename)) {
      USB_SERIAL.printf("PNG file not found: %s\n", filename);
      lastLoadedPngFilename.clear();
      return;
  }

  int16_t rc = png.open(filename, pngOpenLFS, pngClose, pngRead, pngSeek, pngDrawToSprite);

  if (rc != PNG_SUCCESS) {
      USB_SERIAL.printf("png.open() failed: %d\n", rc);
      std::fill(pngPixelBuffer.begin(), pngPixelBuffer.end(), PURPLE);  // Purple on open error
      lastLoadedPngFilename.clear();
      return;
  }

  rc = png.decode(NULL, 0);

  if (rc != PNG_SUCCESS) {
      USB_SERIAL.printf("png.decode() failed: %d\n", rc);
      png.close();
      std::fill(pngPixelBuffer.begin(), pngPixelBuffer.end(), PINK);  // Pink on decode error
      lastLoadedPngFilename.clear();
      return;
  }

  png.close();
  lastLoadedPngFilename = filename;
}

void MapScreen_ex::testDrawPNG(const char* filename, bool swapBytes)
{
  // Test function for manual PNG drawing with verbose output
  // First check if file exists
  if (!LittleFS.exists(filename)) {
      USB_SERIAL.printf("PNG file not found: %s\n", filename);
      return;
  }
  
  // Check file size
  fs::File testFile = LittleFS.open(filename, FILE_READ);
  if (!testFile) {
      USB_SERIAL.printf("Cannot open PNG file: %s\n", filename);
      return;
  }
  uint32_t fileSize = testFile.size();
  testFile.close();
  
  USB_SERIAL.printf("Opening PNG: %s (size: %d bytes)\n", filename, fileSize);
  
  int16_t rc = png.open(filename, pngOpenLFS, pngClose, pngRead, pngSeek, pngDrawToSprite);

  if (rc != PNG_SUCCESS) {
      const char* errorMsg = "Unknown error";
      switch(rc) {
          case 1: errorMsg = "Invalid parameter"; break;
          case 2: errorMsg = "Decode error"; break;
          case 3: errorMsg = "Memory error"; break;
          case 4: errorMsg = "No buffer"; break;
          case 5: errorMsg = "Unsupported feature"; break;
          case 6: errorMsg = "Invalid file"; break;
          case 7: errorMsg = "PNG too big (image width > buffer size)"; break;
          case 8: errorMsg = "Quit early"; break;
      }
      USB_SERIAL.printf("png.open() failed: %d - %s\n", rc, errorMsg);
      USB_SERIAL.printf("Note: PNGdec buffer=%d bytes, supports max %d pixels wide (pitch < %d)\n", 
                        PNG_MAX_BUFFERED_PIXELS, PNG_MAX_BUFFERED_PIXELS/8, PNG_MAX_BUFFERED_PIXELS/2);
      return;
  }

  USB_SERIAL.printf("PNG opened: %dx%d, %d bpp\n", png.getWidth(), png.getHeight(), png.getBpp());

  rc = png.decode(NULL, 0);

  if (rc != PNG_SUCCESS) {
      USB_SERIAL.printf("png.decode() failed: %d\n", rc);
      png.close();
      return;
  }

  png.close();
  
  // Display the decoded PNG buffer directly
  copyFullScreenBufferToDisplay(pngPixelBuffer.data());
}

void MapScreen_ex::drawDiverOnBestFeaturesMapAtCurrentZoom(const double diverLatitude, const double diverLongitude, const double diverHeading)
{
  if (diverLatitude == 0 && diverLongitude == 0 && diverHeading == 0)
  {
    USB_SERIAL.println("MapScreen_ex::drawDiverOnBestFeaturesMapAtCurrentZoom 1: Bypassing draw as 0,0,0 lat,long,heading - no good location received");
    return;
  }

  const uint32_t t0 = micros();

  _lastDiverLatitude = diverLatitude;
  _lastDiverLongitude = diverLongitude;
  _lastDiverHeading = diverHeading;

  bool forceFirstMapDraw = false;

  if (_currentMap == nullptr)
  {
    initCurrentMap(diverLatitude, diverLongitude);
    forceFirstMapDraw=true;
  }

  // Determine the next map - check all lake first
  const geo_map* nextMap;
  if (isAllLakeShown())
  {
    // If showing all lake, always use the all map regardless of position
    nextMap = getMaps() + getAllMapIndex();
    USB_SERIAL.printf("All Lake mode: nextMap=%s (index=%d) currentMap=%s (index=%d)\n", nextMap->label, (int)(nextMap - getMaps()), _currentMap ? _currentMap->label : "null", (_currentMap ? (int)(_currentMap - getMaps()) : -1));
  }
  else
  {
    // Calculate pixel location and use location-based logic
    pixel p = convertGeoToPixelDouble(diverLatitude, diverLongitude, *_currentMap);
    nextMap = getNextMapByPixelLocation(p, _currentMap);
    USB_SERIAL.printf("After getNextMapByPixelLocation: nextMap=%s (index=%d) currentMap=%s (index=%d)\n", nextMap->label, (int)(nextMap - getMaps()), _currentMap ? _currentMap->label : "null", (_currentMap ? (int)(_currentMap - getMaps()) : -1));
  }

  // Now calculate pixel in the correct map coordinate system
  pixel p = convertGeoToPixelDouble(diverLatitude, diverLongitude, *nextMap);

  int16_t prevTileX = _tileXToDisplay;
  int16_t prevTileY = _tileYToDisplay;

  p = scalePixelForZoomedInTile(p,_tileXToDisplay,_tileYToDisplay);

  if (_prevZoom != _zoom)
  {
    forceFirstMapDraw = true;
    _prevZoom = _zoom;
  }

  // Force redraw when entering/exiting all lake mode
  static bool prevShowAllLake = false;
  if (isAllLakeShown() != prevShowAllLake)
  {
    forceFirstMapDraw = true;
    prevShowAllLake = isAllLakeShown();
  }

  const uint32_t t1 = micros();

  if (!useBaseMapCache() || nextMap != _currentMap || prevTileX != _tileXToDisplay || prevTileY != _tileYToDisplay || forceFirstMapDraw)
  {
    USB_SERIAL.printf("MAP REDRAW: nextMap=%s (png=%s) currentMap=%s zoom=%d forceFirstMapDraw=%d\n",
                      nextMap->label, nextMap->png ? nextMap->png : "none",
                      (_currentMap ? _currentMap->label : "null"), _zoom, forceFirstMapDraw);

    if (useBaseMapCache() && nextMap->png)
    {
      USB_SERIAL.printf("  → Loading PNG: %s\n", nextMap->png);
      const uint32_t tPngStart = micros();
      drawPNG(nextMap->png, nextMap->swapBytes);
      const uint32_t tPngEnd = micros();

      if (!pngPixelBuffer.empty())
      {
        const uint32_t tScaleStart = micros();
        _baseMap->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), _zoom, _tileXToDisplay, _tileYToDisplay,
                                                    pngPixelBuffer.data(), nextMap->swapBytes);
        const uint32_t tScaleEnd = micros();

        if (_drawAllFeatures)
        {
          drawFeaturesOnBaseMapSprite(*nextMap, *_baseMap);
        }

        drawMapScaleToSprite(*_baseMap, *nextMap);
        USB_SERIAL.printf("  TIMING: drawPNG=%luus pushImageScaled=%luus\n", tPngEnd-tPngStart, tScaleEnd-tScaleStart);
      }
      else
      {
        _baseMap->fillSprite(nextMap->backColour);
        drawFeaturesOnBaseMapSprite(*nextMap, *_baseMap);
      }
    }
    else if (nextMap->mapData)
    {
      // Flash-based map data (fallback when PNG not available or cache disabled)
      const uint32_t tScaleStart = micros();
      _baseMap->pushImageScaled(0, 0, getTFTWidth(), getTFTHeight(), _zoom, _tileXToDisplay, _tileYToDisplay,
                                                  nextMap->mapData, nextMap->swapBytes);
      const uint32_t tScaleEnd = micros();

      if (_drawAllFeatures)
      {
        drawFeaturesOnBaseMapSprite(*nextMap, *_baseMap);
      }

      drawMapScaleToSprite(*_baseMap, *nextMap);
      USB_SERIAL.printf("  TIMING: pushImageScaled(mapData)=%luus\n", tScaleEnd-tScaleStart);
    }
    else
    {
      _baseMap->fillSprite(nextMap->backColour);
      drawFeaturesOnBaseMapSprite(*nextMap, *_baseMap);
    }
  }

  const uint32_t t2 = micros();

  _baseMapCacheSprite->pushToSprite(*_compositedScreenSprite,0,0);
  const uint32_t t3 = micros();

  drawTracesOnCompositeMapSprite(diverLatitude, diverLongitude, *nextMap);
  const uint32_t t4 = micros();

  drawBreadCrumbTrailOnCompositeMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);
  const uint32_t t5 = micros();

  drawPlacedPins(diverLatitude, diverLongitude, *nextMap);
  const uint32_t t6 = micros();

  drawHeadingLineOnCompositeMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);
  const uint32_t t7 = micros();

  _nearestExitBearing = drawDirectionalLineOnCompositeSprite(diverLatitude, diverLongitude, *nextMap,getClosestJettyIndex(_distanceToNearestExit, true), _mapAttr.nearestExitLineColour, _mapAttr.nearestExitLinePixelLength);
  const uint32_t t8 = micros();

  _targetBearing = drawDirectionalLineOnCompositeSprite(diverLatitude, diverLongitude, *nextMap,_targetWaypointIndex, _mapAttr.targetLineColour, _mapAttr.targetLinePixelLength);
  const uint32_t t9 = micros();

  _targetDistance = distanceBetween(diverLatitude, diverLongitude, WraysburyWaypoints::waypoints[_targetWaypointIndex]._lat, WraysburyWaypoints::waypoints[_targetWaypointIndex]._long);
  _nearestFeatureIndex = getClosestFeatureIndex(_nearestFeatureDistance, true);
  // _nearestFeatureDistance is set by getClosestFeatureIndex — no second distanceBetween call needed
  _nearestFeatureBearing = degreesCourseTo(diverLatitude, diverLongitude, WraysburyWaypoints::waypoints[_nearestFeatureIndex]._lat, WraysburyWaypoints::waypoints[_nearestFeatureIndex]._long);
  const uint32_t t10 = micros();

  writeMapTitleToSprite(*_compositedScreenSprite, *nextMap);
  const uint32_t t11 = micros();

  drawDiverOnCompositedMapSprite(diverLatitude, diverLongitude, diverHeading, *nextMap);
  const uint32_t t12 = micros();

  copyFullScreenSpriteToDisplay(*_compositedScreenSprite);
  const uint32_t t13 = micros();

  USB_SERIAL.printf("DRAW TIMING (us): setup=%lu baseMap=%lu pushToComp=%lu traces=%lu bread=%lu pins=%lu heading=%lu exitLine=%lu targetLine=%lu geo=%lu title=%lu diver=%lu display=%lu TOTAL=%lu\n",
    t1-t0, t2-t1, t3-t2, t4-t3, t5-t4, t6-t5, t7-t6, t8-t7, t9-t8, t10-t9, t11-t10, t12-t11, t13-t12, t13-t0);

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
  return TinyGPSPlus::distanceBetweenAccurate(lat1,long1,lat2,long2);
  /*
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
  */
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
  // Rebuild pixel cache when map changes — convertGeoToPixelDouble is expensive at 1381 points/frame
  if (&featureMap != tracePixelCacheMap)
  {
    const int n = WraysburyTraces::getAllTraceCount();
    tracePixelCache.resize(n);
    for (int i = 0; i < n; i++)
      tracePixelCache[i] = convertGeoToPixelDouble(WraysburyTraces::all_trace[i]._la, WraysburyTraces::all_trace[i]._lo, featureMap);
    tracePixelCacheMap = &featureMap;
    USB_SERIAL.printf("drawTracesOnCompositeMapSprite: rebuilt pixel cache for map '%s' (%d points)\n", featureMap.label, n);
  }

  int16_t diverTileX=0,diverTileY=0;
  pixel diverLocation = convertGeoToPixelDouble(diverLatitude, diverLongitude, featureMap);
  diverLocation = scalePixelForZoomedInTile(diverLocation,diverTileX,diverTileY);

  const int n = WraysburyTraces::getAllTraceCount();
  for (int i = 0; i < n; i++)
  {
    pixel pointLocation = tracePixelCache[i];
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
  
  //    USB_SERIAL.printf("%i,%i      s: %i,%i\n",p.x,p.y,sP.x,sP.y);
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

//    USB_SERIAL.printf("%i,%i      s: %i,%i\n",p.x,p.y,sP.x,sP.y);
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
  USB_SERIAL.printf("dspt x=%i y=%i --> x=%i y=%i  tx=%i ty=%i\n",p.x,p.y,pScaled.x,pScaled.y,tileX,tileY);
}

void MapScreen_ex::debugPixelMapOutput(const MapScreen_ex::pixel loc, const geo_map* thisMap, const geo_map& nextMap) const
{
  USB_SERIAL.printf("dpmo %s %i, %i --> %s\n",thisMap->label,loc.x,loc.y,nextMap.label);
}

void MapScreen_ex::debugPixelFeatureOutput(const NavigationWaypoint& waypoint, MapScreen_ex::pixel loc, const geo_map& thisMap) const
{
  USB_SERIAL.printf("dpfo x=%i y=%i %s %s \n",loc.x,loc.y,thisMap.label,waypoint._label);
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
