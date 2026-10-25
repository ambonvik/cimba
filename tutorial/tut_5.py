# state file generated using paraview version 6.2.0
import paraview
paraview.compatibility.major = 6
paraview.compatibility.minor = 2

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# ----------------------------------------------------------------
# setup views used in the visualization
# ----------------------------------------------------------------

# Create a new 'Render View'
renderView1 = CreateView('RenderView')
renderView1.Set(
    ViewSize=[3101, 1665],
    InteractionMode='Selection',
    AxesGrid='Grid Axes 3D Actor',
    CameraPosition=[122201.86669354464, -517259.7868886029, 186106.78684914453],
    CameraFocalPoint=[-2212.681175749323, 53606.291686739634, -43322.87879544927],
    CameraViewUp=[-0.0447202838265374, 0.36412863727442857, 0.9302743851848989],
)

# init the 'Grid Axes 3D Actor' selected for 'AxesGrid'
renderView1.AxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Grid Actor' selected for 'PolarGrid'
renderView1.PolarGrid.Set(
    AxesLabelBold=0,
    AxesLabelFontSize=12,
)

SetActiveView(None)

# ----------------------------------------------------------------
# setup view layouts
# ----------------------------------------------------------------

# create new layout object 'Layout #1'
layout1 = CreateLayout(name='Layout #1')
layout1.AssignView(0, renderView1)
layout1.SetSize(3101, 1665)

# ----------------------------------------------------------------
# restore active view
SetActiveView(renderView1)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup the selections
# ----------------------------------------------------------------

# create a new 'ID Selection Source'
selectionSource1 = CreateSelection(proxyname='IDSelectionSource', registrationname='SelectionSource1', groupname='selections', FieldType='POINT',
    IDs=[0, 40890])

# create a new 'ID Selection Source'
selectionSource0 = CreateSelection(proxyname='IDSelectionSource', registrationname='SelectionSource0', groupname='selections', FieldType='POINT',
    IDs=[0, 40890])

# create a new 'Append Selections'
appendSelections = CreateSelection(proxyname='AppendSelections', registrationname='AppendSelections', groupname='selections', Input=[selectionSource0, selectionSource1],
    Expression='s0^s1',
    SelectionNames=['s0', 's1'])

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'XML PolyData Reader'
racetrackvtp = XMLPolyDataReader(registrationName='racetrack.vtp', FileName=['/home/ambonvik/Projects/github/cimba/build/tutorial/racetrack.vtp'])
racetrackvtp.Set(
    PointArrayStatus=['Roll_Angle'],
    TimeArray='None',
)

# create a new 'VTKHDF Reader'
terrainvtkhdf = VTKHDFReader(registrationName='terrain.vtkhdf', FileName=['/home/ambonvik/Projects/github/cimba/build/tutorial/terrain.vtkhdf'])
terrainvtkhdf.PointArrayStatus = ['Elevation']

# create a new 'VTKHDF Reader'
eventsvtkhdf = VTKHDFReader(registrationName='events.vtkhdf', FileName=['/home/ambonvik/Projects/github/cimba/build/tutorial/events.vtkhdf'])
eventsvtkhdf.PointArrayStatus = ['Is_AWACS', 'RCS', 'Detection_Status', 'AWACS_Direction', 'Sensor_Direction']

# create a new 'Threshold'
targets = Threshold(registrationName='Targets', Input=eventsvtkhdf)
targets.Set(
    Scalars=['POINTS', 'Is_AWACS'],
    LowerThreshold=-0.5,
    UpperThreshold=0.5,
)

# create a new 'Calculator'
log_RCS = Calculator(registrationName='Log_RCS', Input=targets)
log_RCS.Set(
    ResultArrayName='log_RCS',
    Function='log(RCS+1)',
)

# create a new 'Glyph'
glyph4 = Glyph(registrationName='Glyph4', Input=log_RCS,
    GlyphType='Sphere')
glyph4.Set(
    OrientationArray=['POINTS', 'No orientation array'],
    ScaleArray=['POINTS', 'log_RCS'],
    ScaleFactor=2500.0,
    GlyphTransform='Transform2',
    GlyphMode='All Points',
)

# create a new 'Threshold'
aWACS = Threshold(registrationName='AWACS', Input=eventsvtkhdf)
aWACS.Set(
    Scalars=['POINTS', 'Is_AWACS'],
    LowerThreshold=0.5,
    UpperThreshold=1.5,
)

# create a new 'Glyph'
glyph3 = Glyph(registrationName='Glyph3', Input=aWACS,
    GlyphType='Arrow')
glyph3.Set(
    OrientationArray=['POINTS', 'Sensor_Direction'],
    ScaleArray=['POINTS', 'Sensor_Direction'],
    ScaleFactor=10000.0,
    GlyphTransform='Transform2',
    GlyphMode='All Points',
)

# create a new 'Glyph'
glyph2 = Glyph(registrationName='Glyph2', Input=aWACS,
    GlyphType='Arrow')
glyph2.Set(
    OrientationArray=['POINTS', 'AWACS_Direction'],
    ScaleArray=['POINTS', 'No scale array'],
    ScaleFactor=10000.0,
    GlyphTransform='Transform2',
    GlyphMode='All Points',
)

# create a new 'Glyph'
glyph1 = Glyph(registrationName='Glyph1', Input=aWACS,
    GlyphType='Sphere')
glyph1.Set(
    OrientationArray=['POINTS', 'No orientation array'],
    ScaleArray=['POINTS', 'No scale array'],
    ScaleFactor=5000.0,
    GlyphTransform='Transform2',
    GlyphMode='All Points',
)

appendSelections.SetSelectionId(glyph4.GetGlobalID())
appendSelections.SetSelectionPort(0)

# ----------------------------------------------------------------
# setup the visualization in view 'renderView1'
# ----------------------------------------------------------------

# show data from terrainvtkhdf
terrainvtkhdfDisplay = Show(terrainvtkhdf, renderView1, 'UniformGridRepresentation')

# get color transfer function/color map for 'Elevation'
elevationLUT = GetColorTransferFunction('Elevation')
elevationLUT.Set(
    RGBPoints=GenerateRGBPoints(
        preset_name='Linear Green (Gr4L)',
        range_min=51.61174392700195,
        range_max=2490.738525390625,
    ),
    NanColor=[0.25, 0.0, 0.0],
    ScalarRangeInitialized=1.0,
)

# trace defaults for the display properties.
terrainvtkhdfDisplay.Set(
    Representation='Slice',
    ColorArrayName=['POINTS', 'Elevation'],
    LookupTable=elevationLUT,
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
terrainvtkhdfDisplay.ScaleTransferFunction.Points = [88.1886978149414, 0.0, 0.5, 0.0, 2490.738525390625, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
terrainvtkhdfDisplay.OpacityTransferFunction.Points = [88.1886978149414, 0.0, 0.5, 0.0, 2490.738525390625, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
terrainvtkhdfDisplay.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
terrainvtkhdfDisplay.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from racetrackvtp
racetrackvtpDisplay = Show(racetrackvtp, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
racetrackvtpDisplay.Set(
    Representation='Surface',
    ColorArrayName=[None, ''],
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
racetrackvtpDisplay.ScaleTransferFunction.Points = [0.0, 0.0, 0.5, 0.0, 7.471499919891357, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
racetrackvtpDisplay.OpacityTransferFunction.Points = [0.0, 0.0, 0.5, 0.0, 7.471499919891357, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
racetrackvtpDisplay.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
racetrackvtpDisplay.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from aWACS
aWACSDisplay = Show(aWACS, renderView1, 'UnstructuredGridRepresentation')

# trace defaults for the display properties.
aWACSDisplay.Set(
    Representation='Surface',
    ColorArrayName=[None, ''],
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
aWACSDisplay.ScaleTransferFunction.Points = [-4.371138828673793e-08, 0.0, 0.5, 0.0, -4.3704112329123745e-08, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
aWACSDisplay.OpacityTransferFunction.Points = [-4.371138828673793e-08, 0.0, 0.5, 0.0, -4.3704112329123745e-08, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
aWACSDisplay.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
aWACSDisplay.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from glyph2
glyph2Display = Show(glyph2, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
glyph2Display.Set(
    Representation='Surface',
    AmbientColor=[1.0, 0.0, 0.0],
    ColorArrayName=[None, ''],
    DiffuseColor=[1.0, 0.0, 0.0],
    Specular=1.0,
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
glyph2Display.ScaleTransferFunction.Points = [-4.371138828673793e-08, 0.0, 0.5, 0.0, -4.3704112329123745e-08, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
glyph2Display.OpacityTransferFunction.Points = [-4.371138828673793e-08, 0.0, 0.5, 0.0, -4.3704112329123745e-08, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
glyph2Display.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
glyph2Display.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from glyph3
glyph3Display = Show(glyph3, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
glyph3Display.Set(
    Representation='Surface',
    AmbientColor=[1.0, 0.0, 0.0],
    ColorArrayName=[None, ''],
    DiffuseColor=[1.0, 0.0, 0.0],
    Specular=1.0,
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
glyph3Display.ScaleTransferFunction.Points = [-4.371138828673793e-08, 0.0, 0.5, 0.0, -4.3704112329123745e-08, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
glyph3Display.OpacityTransferFunction.Points = [-4.371138828673793e-08, 0.0, 0.5, 0.0, -4.3704112329123745e-08, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
glyph3Display.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
glyph3Display.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from log_RCS
log_RCSDisplay = Show(log_RCS, renderView1, 'UnstructuredGridRepresentation')

# trace defaults for the display properties.
log_RCSDisplay.Set(
    Representation='Surface',
    ColorArrayName=['POINTS', ''],
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
log_RCSDisplay.ScaleTransferFunction.Points = [1.791759469228055, 0.0, 0.5, 0.0, 4.61512051684126, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
log_RCSDisplay.OpacityTransferFunction.Points = [1.791759469228055, 0.0, 0.5, 0.0, 4.61512051684126, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
log_RCSDisplay.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
log_RCSDisplay.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from glyph4
glyph4Display = Show(glyph4, renderView1, 'GeometryRepresentation')

# get color transfer function/color map for 'Detection_Status'
detection_StatusLUT = GetColorTransferFunction('Detection_Status')
detection_StatusLUT.Set(
    InterpretValuesAsCategories=1,
    AnnotationsInitialized=1,
    AutomaticRescaleRangeMode='Never',
    ScalarRangeInitialized=1.0,
    Annotations=['0', 'Undetermined', '1', 'Beyond Horizon', '2', 'In Nadir Hole', '3', 'Terrain Shielded', '4', 'Missed (Clutter)', '5', 'Detected'],
    IndexedColors=[0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.6666666865348816, 0.0, 1.0, 1.0, 0.6666666865348816, 0.0, 0.8666666746139526, 0.8666666746139526, 0.0, 1.0, 1.0, 1.0],
    IndexedOpacities=[1.0, 1.0, 1.0, 1.0, 1.0, 1.0],
)

# trace defaults for the display properties.
glyph4Display.Set(
    Representation='Surface',
    ColorArrayName=['POINTS', 'Detection_Status'],
    LookupTable=detection_StatusLUT,
    SelectNormalArray='Normals',
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
glyph4Display.ScaleTransferFunction.Points = [1.791759469228055, 0.0, 0.5, 0.0, 4.61512051684126, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
glyph4Display.OpacityTransferFunction.Points = [1.791759469228055, 0.0, 0.5, 0.0, 4.61512051684126, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
glyph4Display.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
glyph4Display.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# show data from glyph1
glyph1Display = Show(glyph1, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
glyph1Display.Set(
    Representation='Surface',
    AmbientColor=[1.0, 0.0, 0.0],
    ColorArrayName=[None, ''],
    DiffuseColor=[1.0, 0.0, 0.0],
    Specular=1.0,
    SelectNormalArray='Normals',
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# init the 'Piecewise Function' selected for 'ScaleTransferFunction'
glyph1Display.ScaleTransferFunction.Points = [1.1924880638503055e-08, 0.0, 0.5, 0.0, 1.19266996279066e-08, 1.0, 0.5, 0.0]

# init the 'Piecewise Function' selected for 'OpacityTransferFunction'
glyph1Display.OpacityTransferFunction.Points = [1.1924880638503055e-08, 0.0, 0.5, 0.0, 1.19266996279066e-08, 1.0, 0.5, 0.0]

# init the 'Grid Axes Representation' selected for 'DataAxesGrid'
glyph1Display.DataAxesGrid.Set(
    XTitleBold=0,
    XTitleFontSize=12,
    YTitleBold=0,
    YTitleFontSize=12,
    ZTitleBold=0,
    ZTitleFontSize=12,
    XLabelBold=0,
    XLabelFontSize=12,
    YLabelBold=0,
    YLabelFontSize=12,
    ZLabelBold=0,
    ZLabelFontSize=12,
)

# init the 'Polar Axes Representation' selected for 'PolarAxes'
glyph1Display.PolarAxes.Set(
    PolarAxisTitleBold=0,
    PolarAxisTitleFontSize=12,
    PolarAxisLabelBold=0,
    PolarAxisLabelFontSize=12,
    LastRadialAxisTextBold=0,
    LastRadialAxisTextFontSize=12,
    SecondaryRadialAxesTextBold=0,
    SecondaryRadialAxesTextFontSize=12,
)

# setup the color legend parameters for each legend in this view

# get color legend/bar for elevationLUT in view renderView1
elevationLUTColorBar = GetScalarBar(elevationLUT, renderView1)
elevationLUTColorBar.Set(
    Title='Elevation',
    ComponentTitle='',
    AllowOverlappingLabels=1,
)

# set color bar visibility
elevationLUTColorBar.Visibility = 1

# get color legend/bar for detection_StatusLUT in view renderView1
detection_StatusLUTColorBar = GetScalarBar(detection_StatusLUT, renderView1)
detection_StatusLUTColorBar.Set(
    WindowLocation='Upper Right Corner',
    Title='Detection_Status',
    ComponentTitle='',
    AllowOverlappingLabels=1,
)

# set color bar visibility
detection_StatusLUTColorBar.Visibility = 1

# show color legend
terrainvtkhdfDisplay.SetScalarBarVisibility(renderView1, True)

# show color legend
glyph4Display.SetScalarBarVisibility(renderView1, True)

# ----------------------------------------------------------------
# setup color maps and opacity maps used in the visualization
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get opacity transfer function/opacity map for 'Detection_Status'
detection_StatusPWF = GetOpacityTransferFunction('Detection_Status')
detection_StatusPWF.Set(
    Points=[0.0, 0.0, 0.5, 0.0, 5.0, 1.0, 0.5, 0.0],
    ScalarRangeInitialized=1,
)

# get opacity transfer function/opacity map for 'Elevation'
elevationPWF = GetOpacityTransferFunction('Elevation')
elevationPWF.Set(
    Points=[51.61174392700195, 0.0, 0.5, 0.0, 2490.738525390625, 1.0, 0.5, 0.0],
    ScalarRangeInitialized=1,
)

# ----------------------------------------------------------------
# setup animation scene, tracks and keyframes
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get time animation track
timeAnimationCue1 = GetTimeTrack()

# initialize the animation scene

# get the time-keeper
timeKeeper1 = GetTimeKeeper()

# initialize the timekeeper

# initialize the animation track

# get animation scene
animationScene1 = GetAnimationScene()

# initialize the animation scene
animationScene1.Set(
    ViewModules=renderView1,
    Cues=timeAnimationCue1,
    AnimationTime=0.0,
    EndTime=21599.0,
    PlayMode='Snap To TimeSteps',
)

# ----------------------------------------------------------------
# restore active source
SetActiveSource(glyph1)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup camera links
# ----------------------------------------------------------------


##--------------------------------------------
## You may need to add some code at the end of this python script depending on your usage, eg:
#
## Render all views to see them appears
# RenderAllViews()
#
## Interact with the view, usefull when running from pvpython
# Interact()
#
## Save a screenshot of the active view
# SaveScreenshot("path/to/screenshot.png")
#
## Save a screenshot of a layout (multiple splitted view)
# SaveScreenshot("path/to/screenshot.png", GetLayout())
#
## Save all "Extractors" from the pipeline browser
# SaveExtracts()
#
## Save a animation of the current active view
# SaveAnimation()
#
## Please refer to the documentation of paraview.simple
## https://www.paraview.org/paraview-docs/nightly/python/
##--------------------------------------------