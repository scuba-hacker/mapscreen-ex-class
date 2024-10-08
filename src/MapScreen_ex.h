#ifndef MapScreen_ex_h
#define MapScreen_ex_h

#include <stdint.h>
#include <memory>
#include <array>

class TFT_eSPI;
class TFT_eSprite;
class NavigationWaypoint;
class Print;

/* Requirements:
 *  
 *  DONE - survey maps are checked for presence before non-survey maps.
 *  DONE - survey maps show all features within map extent, plus diver, without last visited feature shown.
 *  DONE - survey maps only switch to last zoom non-survey map when out of area.
 *  DONE - diver sprite is rotated according to compass direction.
 *  DONE - at zoom level 1 and 2, base map gets switched once the selectMap function has detected diver moved to edge as defined by current map.
 *  DONE - at zoom level 2 switch between tiles within a base map is done at a tile boundary.
 *  DONE - for non-survey maps, last feature and next feature are shown in different colours.
 *  DONE - Heading indicator in blue
 *  DONE - Direction Line in red to next feature spanning maps and tiles at any zoom
 *  DONE - Green Line pointing to nearest exit Cafe or Mid Jetty
 *  DONE - breadcrumb trail test
 *  DONE - breadcrumb record with entire system - upload useraction=2
 *  TODO - flash Diver Sprite Pink/Yellow when recording a new PIN location.
 */
 
class MapScreen_ex
{  
   public:
    class pixel
    {
      public:
        pixel() : x(0), y(0), colour(0) {}
        pixel(int16_t xx, int16_t yy, uint16_t colourr) : x(xx), y(yy), colour(colourr) {}
        pixel(int16_t xx, int16_t yy) : x(xx), y(yy), colour(0) {}

        int16_t x;
        int16_t y;
        uint16_t colour;
    };

    class MapScreenAttr
    {
      public:
        int diverSpriteColour;
        uint8_t diverSpriteRadius;

        uint16_t headingIndicatorColour;
        uint16_t headingIndicatorRadius;
        uint16_t headingIndicatorOffsetX;
        uint16_t headingIndicatorOffsetY;

        uint16_t diverHeadingColour;
        int diverHeadingLinePixelLength;

        int featureSpriteColour;
        uint8_t featureSpriteRadius;

        uint16_t targetSpriteColour;
        uint16_t lastTargetSpriteColour;

        int nearestExitLineColour;
        int nearestExitLinePixelLength;

        int targetLineColour;
        int targetLinePixelLength;

        int breadCrumbColour;
        int breadCrumbWidth;
        int breadCrumbDropFixCount;

        int pinBackColour;
        int pinForeColour;
        int pinWidth;

        bool useSpriteForFeatures;

        bool traceColour;
        int tracePointSize;
    };

    class geo_map
    {
      public:
        const uint16_t* mapData;
        const char* label;
        const uint16_t backColour;
        const char* backText;
        const bool surveyMap;
        const bool swapBytes;
        const float mapLongitudeLeft;
        const float mapLongitudeRight;
        const float mapLatitudeBottom;
      
        geo_map(const uint16_t * md, const char* l, uint16_t bc,const char* bt, bool sm, bool sb, float ll, float lr, float lb) : mapData(md),label(l),backColour(bc),backText(bt),surveyMap(sm),swapBytes(sb),mapLongitudeLeft(ll),mapLongitudeRight(lr),mapLatitudeBottom(lb)
        {}
    };

    class geoRef
    {
      static const int geoMapsSize=10;
      public:
        int geoMaps[geoMapsSize];
    };

    class BreadCrumb
    {
      public:
        double _lat;
        double _long;
        double _heading;
        double _depth;

      BreadCrumb(const double lat=0.0, const double lng=0.0, const double heading=0.0, const double depth=0.0) : _lat(lat),_long(lng),_heading(heading),_depth(depth) {}
    };

    class TracePoint
    {
      public:
        double _lat;
        double _long;

      TracePoint(double lat = 0.0, double lng=0.0) : _lat(lat),_long(lng) {}
    };

    protected:
        const MapScreenAttr _mapAttr;
        int _exitWaypointCount;
      
        virtual pixel getRegistrationMarkLocation(int index) = 0;
        virtual int getRegistrationMarkLocationsSize() = 0;

        virtual int getFirstDetailMapIndex() = 0;
        virtual int getEndDetailMaps() = 0;
        virtual int getAllMapIndex() = 0;
        virtual const geo_map* getMaps() = 0;

        void initMaps()
        {
          _maps = getMaps();
        }

        void initFeatureColours();

        static const uint8_t maxWaypointColours = 10;
        uint16_t waypointColourLookup[maxWaypointColours];

        virtual const geo_map* getNextMapByPixelLocation(MapScreen_ex::pixel loc, const geo_map* thisMap) = 0;

        virtual bool useBaseMapCache() const = 0;

  public:
    MapScreen_ex(TFT_eSPI& tft,const MapScreenAttr mapAttributes);
    
    ~MapScreen_ex()
    {
    }
  
    virtual void initMapScreen();

    Print* LOG_HOOK;

    void provideLoggingHook(Print& hook)
    {
      LOG_HOOK = &hook;
    }

    void (*_recordActionCallback)(const bool);

    void registerBreadCrumbRecordActionCallback(void (*recordActionCallback)(const bool))
    {
      _recordActionCallback = recordActionCallback;
    }
    
    virtual int16_t getTFTWidth() const = 0;
    virtual int16_t getTFTHeight() const = 0;
    
    void setTargetWaypointByLabel(const char* label);

    void setUseDiverHeading(const bool use)
    {
      _useDiverHeading = use;
    }
    
    void initCurrentMap(const double diverLatitude, const double diverLongitude);
    void clearMap();
    virtual void fillScreen(int colour) = 0;

    void drawFeaturesOnSpecifiedMapToScreen(int featureIndex, int16_t zoom=1, int16_t tileX=0, int16_t tileY=0);
    void drawFeaturesOnSpecifiedMapToScreen(const geo_map& featureAreaToShow, int16_t zoom=1, int16_t tileX=0, int16_t tileY=0);
    void drawDiverOnBestFeaturesMapAtCurrentZoom(const double diverLatitude, const double diverLongitude, const double diverHeading = 0);
    void drawDiverOnCompositedMapSprite(const double latitude, const double longitude, const double heading, const geo_map& featureMap);
    void writeOverlayTextToCompositeMapSprite();
    
    virtual void drawMapScaleToSprite(TFT_eSprite& sprite, const geo_map& featureMap)
    {
      // no scale by default
    }

    TFT_eSprite& getCompositeSprite();
    TFT_eSprite& getBaseMapSprite();

    double distanceBetween(double lat1, double long1, double lat2, double long2) const;
    double degreesCourseTo(double lat1, double long1, double lat2, double long2) const;
    double radiansCourseTo(double lat1, double long1, double lat2, double long2) const;

    int getClosestJettyIndex(double& distance);
    int getClosestFeatureIndex(double& distance);
 
    int drawDirectionalLineOnCompositeSprite(const double diverLatitude, const double diverLongitude, 
                                                    const geo_map& featureMap, const int waypointIndex, uint16_t colour, int indicatorLength);

    void placePin(const double lat, const double lng, const double head, const double dep);

    void drawPlacedPins(const double diverLatitude, const double diverLongitude, const geo_map& featureMap);

    void drawTracesOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, const geo_map& featureMap);

    void drawBreadCrumbTrailOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, 
                                                            const double heading, const geo_map& featureMap);

    void drawHeadingLineOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, 
                                            const double heading, const geo_map& featureMap);

    void drawRegistrationPixelsOnBaseMapSprite(const geo_map& featureMap);

    void cycleZoom();
    
    bool isAllLakeShown() const { return _showAllLake; }
    void setAllLakeShown(bool showAll);

    int16_t getZoom() const     { return _zoom; }
    void setZoom(const int16_t zoom);

    void setDrawAllFeatures(const bool showAll)
    { 
      _drawAllFeatures = showAll;
      _currentMap = nullptr;
    }

    void toggleDrawAllFeatures()
    {
      setDrawAllFeatures(!getDrawAllFeatures());
    }

    bool getDrawAllFeatures() const
    { return _drawAllFeatures; }

    void toggleShowBreadCrumbTrail();
    void toggleRecordBreadCrumbTrail();
    void setBreadCrumbTrailRecord(const bool enable);
    void clearBreadCrumbTrail();

    void testAnimatingDiverSpriteOnCurrentMap();
    void testDrawingMapsAndFeatures(uint8_t& currentMap, int16_t& zoom);

    virtual void copyCompositeSpriteToDisplay()
    {
      copyFullScreenSpriteToDisplay(*_compositedScreenSprite);
    }
    
    void displayMapLegend();

  protected:
    int16_t _zoom;
    int16_t _prevZoom;

    TFT_eSPI& _tft;

  private:
    std::shared_ptr<TFT_eSprite> _baseMap;

    std::shared_ptr<TFT_eSprite> _baseMapCacheSprite;
    std::shared_ptr<TFT_eSprite> _compositedScreenSprite;
    std::unique_ptr<TFT_eSprite> _diverSprite;
    std::unique_ptr<TFT_eSprite> _diverPlainSprite;
    std::unique_ptr<TFT_eSprite> _diverRotatedSprite;
    std::unique_ptr<TFT_eSprite> _featureSprite;
    std::unique_ptr<TFT_eSprite> _targetSprite;
    std::unique_ptr<TFT_eSprite> _lastTargetSprite;
    std::unique_ptr<TFT_eSprite> _breadCrumbSprite;
    std::unique_ptr<TFT_eSprite> _rotatedBreadCrumbSprite;
    std::unique_ptr<TFT_eSprite> _pinSprite;

    bool _useDiverHeading;
    
    const geo_map* _maps;

    const geo_map* _currentMap;

    bool _showAllLake;

    virtual void writeMapTitleToSprite(TFT_eSprite& sprite, const geo_map& map) = 0;
    virtual void copyFullScreenSpriteToDisplay(TFT_eSprite& sprite) = 0;
   
    int16_t _tileXToDisplay;
    int16_t _tileYToDisplay;

    bool _drawAllFeatures;

    static const int _maxBreadCrumbs=1000;
    BreadCrumb _breadCrumbTrail[_maxBreadCrumbs];
    bool _showBreadCrumbTrail = true;
    bool _recordBreadCrumbTrail = false;
    int _nextCrumbIndex=0;
    uint8_t _breadCrumbCountDown = 0;

    static const int _maxPlacedPins = 50;
    BreadCrumb _placedPins[_maxPlacedPins];
    int _placedPinIndex = 0;

    static const int s_exitWaypointSize=10; 
    std::array<int,s_exitWaypointSize> _exitWaypointIndices;
 
    void initSprites();
    void initExitWaypoints();

    void drawFeaturesOnBaseMapSprite(const geo_map& featureMap, TFT_eSprite& sprite);
    
    MapScreen_ex::pixel scalePixelForZoomedInTile(const pixel p, int16_t& tileX, int16_t& tileY) const;

    virtual bool isPixelInCanoeZone(const MapScreen_ex::pixel loc, const geo_map& thisMap) const = 0;
    virtual bool isPixelInSubZone(const MapScreen_ex::pixel loc, const geo_map& thisMap) const = 0;

    void debugPixelMapOutput(const MapScreen_ex::pixel loc, const geo_map* thisMap, const geo_map& nextMap) const;
    void debugPixelFeatureOutput(const NavigationWaypoint& waypoint, MapScreen_ex::pixel loc, const geo_map& thisMap) const;
    void debugScaledPixelForTile(pixel p, pixel pScaled, int16_t tileX,int16_t tileY) const;

protected:
    struct BoundingBox
    {
      MapScreen_ex::pixel topLeft;
      MapScreen_ex::pixel botRight;
      const geo_map& map;

      BoundingBox(const MapScreen_ex::pixel tl, const MapScreen_ex::pixel br, const geo_map& m) : 
        topLeft(tl), botRight(br), map(m)
        { 

        }
      bool withinBox(pixel l, const geo_map& m) const
      {
        return (&m == &map && l.x >= topLeft.x && l.y >= topLeft.y && l.x <= botRight.x && l.y <= botRight.y);
      }
    };

    bool isPixelOutsideScreenExtent(const MapScreen_ex::pixel loc) const;
    pixel convertGeoToPixelDouble(double latitude, double longitude, const geo_map& mapToPlot) const;

  double _distanceToNearestExit = 0.0;  
  double _nearestExitBearing = 0.0;

  double _nearestFeatureDistance = 0.0;
  double _nearestFeatureBearing = 0.0;
  
  double _targetBearing = 0.0;
  double _targetDistance = 0.0;

  double _lastDiverLatitude;
  double _lastDiverLongitude;
  double _lastDiverHeading;

  int _targetWaypointIndex;
  int _prevWaypointIndex;
  int _nearestFeatureIndex;
};

#endif
