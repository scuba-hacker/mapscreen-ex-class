#ifndef MapScreen_ex_h
#define MapScreen_ex_h

#include <stdint.h>
#include <memory>
#include <array>

class TFT_eSPI;
class TFT_eSprite;
class NavigationWaypoint;

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
 *  TODO - flash Diver Sprite Pink/Yellow when recording a new PIN location.
 *  TODO - Diver sprite flashes blue/green.
 *  TODO - breadcrumb trail
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

        int featureSpriteColour;
        uint8_t featureSpriteRadius;

        uint16_t targetSpriteColour;
        uint16_t lastTargetSpriteColour;

        int directionLineColour;
        int directionLinePixelLength;

        int targetLineColour;
        int targetLinePixelLength;

        bool useSpriteForFeatures;
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
      static const int geoMapsSize=10;  // should be same number as number of maps? - set to arbitrarily larger size.
      public:
        int geoMaps[geoMapsSize];       // MBJ REFACTOR to std::array
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

        virtual const geo_map* getNextMapByPixelLocation(MapScreen_ex::pixel loc, const geo_map* thisMap) = 0;

  public:
    MapScreen_ex(TFT_eSPI& tft,const MapScreenAttr mapAttributes);
    
    ~MapScreen_ex()
    {
    }
    
    virtual void initMapScreen();
    
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

    TFT_eSprite& getCompositeSprite();
    TFT_eSprite& getCleanMapSprite();

    double distanceBetween(double lat1, double long1, double lat2, double long2) const;
    double degreesCourseTo(double lat1, double long1, double lat2, double long2) const;
    double radiansCourseTo(double lat1, double long1, double lat2, double long2) const;

    const int getClosestJettyIndex(double& distance);

    int drawDirectionLineOnCompositeSprite(const double diverLatitude, const double diverLongitude, 
                                                    const geo_map& featureMap, const int waypointIndex, uint16_t colour, int indicatorLength);

    void drawHeadingLineOnCompositeMapSprite(const double diverLatitude, const double diverLongitude, 
                                            const double heading, const geo_map& featureMap);

    void drawRegistrationPixelsOnCleanMapSprite(const geo_map& featureMap);

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

    void testAnimatingDiverSpriteOnCurrentMap();
    void testDrawingMapsAndFeatures(uint8_t& currentMap, int16_t& zoom);

    virtual void copyCompositeSpriteToDisplay()
    {
      copyFullScreenSpriteToDisplay(*_compositedScreenSprite);
    }

  protected:
    int16_t _zoom;
    int16_t _priorToZoneZoom;

    TFT_eSPI& _tft;

  private:
    
    std::unique_ptr<TFT_eSprite> _cleanMapAndFeaturesSprite;
    std::shared_ptr<TFT_eSprite> _compositedScreenSprite;
    std::unique_ptr<TFT_eSprite> _diverSprite;
    std::unique_ptr<TFT_eSprite> _diverPlainSprite;
    std::unique_ptr<TFT_eSprite> _diverRotatedSprite;
    std::unique_ptr<TFT_eSprite> _featureSprite;
    std::unique_ptr<TFT_eSprite> _targetSprite;
    std::unique_ptr<TFT_eSprite> _lastTargetSprite;

    double _lastDiverLatitude;
    double _lastDiverLongitude;
    double _lastDiverHeading;

    bool _useDiverHeading;
    
    const geo_map* _maps;

    const geo_map* _currentMap;

    bool _showAllLake;

    virtual void writeMapTitleToSprite(TFT_eSprite& sprite, const geo_map& map) = 0;
    virtual void copyFullScreenSpriteToDisplay(TFT_eSprite& sprite) = 0;

    int _targetWaypointIndex;
    int _prevWaypointIndex;
   
    int16_t _tileXToDisplay;
    int16_t _tileYToDisplay;

    bool _drawAllFeatures;

    static const int s_exitWaypointSize=10; 
    std::array<int,s_exitWaypointSize> _exitWaypointIndices;
 
    void initSprites();
    void initExitWaypoints();

    void drawFeaturesOnCleanMapSprite(const geo_map& featureMap);
    
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
};

#endif
