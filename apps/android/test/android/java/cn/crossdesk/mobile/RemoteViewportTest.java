package cn.crossdesk.mobile;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

/** Non-UI regression checks for zoom and remote input geometry. */
@RunWith(AndroidJUnit4.class)
public class RemoteViewportTest {
    private static final float EPSILON = .0001f;

    @Test public void fittedVideoRejectsLetterboxingAndMapsItsEdges() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(1080,1920,1920,1080);
        assertEquals(1080,viewport.baseWidth());
        assertEquals(607,viewport.baseHeight());
        assertEquals(656,viewport.top(),EPSILON);
        assertFalse(viewport.contains(540,200));
        assertTrue(viewport.contains(0,656));
        assertFalse(viewport.contains(1080,1263));
        assertEquals(.5f,viewport.normalizedX(540),EPSILON);
        assertEquals(.5f,viewport.normalizedY(959.5f),EPSILON);
    }

    @Test public void pinchKeepsTheRemotePointUnderAnOffCenterFocus() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(1000,600,1000,600);
        viewport.zoomBy(2,750,350);
        assertEquals(2,viewport.scale(),EPSILON);
        assertEquals(-250,viewport.offsetX(),EPSILON);
        assertEquals(-50,viewport.offsetY(),EPSILON);
        assertEquals(.75f,viewport.normalizedX(750),EPSILON);
        assertEquals(350f/600,viewport.normalizedY(350),EPSILON);
        viewport.zoomBy(.5f,750,350);
        assertEquals(1,viewport.scale(),EPSILON);
        assertEquals(0,viewport.offsetX(),EPSILON);
        assertEquals(0,viewport.offsetY(),EPSILON);
    }

    @Test public void zoomAndPanUseTheSameInverseTransformForRemoteInput() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(1200,800,1920,1080);
        viewport.zoomBy(3,600,399.5f);
        viewport.panBy(200,100);
        assertEquals(-1000,viewport.left(),EPSILON);
        assertEquals(-513,viewport.top(),EPSILON);
        assertEquals(1380f/3600,viewport.normalizedX(380),EPSILON);
        assertEquals(.28f,viewport.normalizedY(54),EPSILON);
        // Trackpad deltas use the zoomed dimensions, too.
        assertEquals(.01f,36/viewport.width(),EPSILON);
        assertEquals(.01f,20.25f/viewport.height(),EPSILON);
    }

    @Test public void panCannotMoveTheImagePastTheOriginalFittedArea() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(1000,800,1600,900);
        viewport.zoomBy(2,500,400);
        viewport.panBy(10000,-10000);
        assertEquals(500,viewport.offsetX(),EPSILON);
        assertEquals(-281,viewport.offsetY(),EPSILON);
        assertEquals(0,viewport.left(),EPSILON);
        assertEquals(119,viewport.top()+viewport.height()-viewport.baseHeight(),EPSILON);
        viewport.panBy(-20000,20000);
        assertEquals(-500,viewport.offsetX(),EPSILON);
        assertEquals(281,viewport.offsetY(),EPSILON);
    }

    @Test public void scaleMatchesIosLimitsAndFinishingNearOneRestoresFit() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(1000,600,1000,600);
        viewport.zoomBy(100,500,300);
        assertEquals(10,viewport.scale(),EPSILON);
        viewport.zoomBy(.10005f,500,300);
        viewport.panBy(.1f,.1f);
        viewport.finishGesture();
        assertEquals(1,viewport.scale(),EPSILON);
        assertEquals(0,viewport.offsetX(),EPSILON);
        assertEquals(0,viewport.offsetY(),EPSILON);
        assertFalse(viewport.isZoomed());
        viewport.zoomBy(.01f,500,300);
        assertEquals(1,viewport.scale(),EPSILON);
    }

    @Test public void rotationAndResolutionChangesConstrainExistingPan() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(1000,600,1000,600);
        viewport.zoomBy(2,500,300);
        viewport.panBy(500,300);
        viewport.fit(600,1000,1000,600);
        assertEquals(2,viewport.scale(),EPSILON);
        assertEquals(300,viewport.offsetX(),EPSILON);
        assertEquals(180,viewport.offsetY(),EPSILON);
        viewport.fit(600,1000,600,1000);
        assertEquals(2,viewport.scale(),EPSILON);
        assertEquals(1000,viewport.baseHeight());
        viewport.reset();
        assertEquals(1,viewport.scale(),EPSILON);
        assertEquals(0,viewport.offsetX(),EPSILON);
        assertEquals(0,viewport.offsetY(),EPSILON);
    }

    @Test public void oddMarginsShareTheActualSurfaceCenterAtMaximumZoom() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(801,603,800,600);
        assertEquals(801,viewport.baseWidth());
        assertEquals(600,viewport.baseHeight());
        assertEquals(1,viewport.top(),EPSILON);
        viewport.zoomBy(10,400.5f,301);
        assertEquals(-3604.5f,viewport.left(),EPSILON);
        assertEquals(-2699,viewport.top(),EPSILON);
        assertEquals(.5f,viewport.normalizedX(400.5f),EPSILON);
        assertEquals(.5f,viewport.normalizedY(301),EPSILON);
    }

    @Test public void invalidGeometryAndScaleDoNotCorruptViewport() {
        RemoteViewport viewport = new RemoteViewport();
        viewport.fit(0,0,1920,1080);
        assertFalse(viewport.contains(0,0));
        viewport.zoomBy(2,0,0);
        assertEquals(1,viewport.scale(),EPSILON);
        viewport.fit(1000,600,1000,600);
        viewport.zoomBy(Float.NaN,500,300);
        viewport.zoomBy(Float.POSITIVE_INFINITY,500,300);
        viewport.zoomBy(-1,500,300);
        assertEquals(1,viewport.scale(),EPSILON);
        assertEquals(0,viewport.offsetX(),EPSILON);
    }
}
