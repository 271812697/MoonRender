<MoonDocument version="1">
    <Body name="Body">
        <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
        <Feature type="SketcherFeature" name="sketcher" tag="Sketcher" base="-1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters>
                <Sketch origin="0 0 0" xAxis="1 0 0" yAxis="0 0 -1" normal="0 1 0" drawGrid="1" snapGrid="0">
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="8 40 0" p2="0 40 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0 40 0" p2="0 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0 0 0" p2="40 -2.4490602417142487e-15 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="40 -2.4490602417142487e-15 0" p2="40.00000000000022 19.000000000001222 0"/>
                    <Geometry type="GeomArcOfCircle" construction="0" visible="1" center="49.343830155731524 68.119169761112275 0" radius="50" first="3.7388580451171376" last="4.5244072085168519"/>
                    <Constraint type="10" first="0" firstPos="2" second="1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="10" first="1" firstPos="2" second="2" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="10" first="2" firstPos="2" second="3" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="0" firstPos="1" second="4" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="3" firstPos="2" second="4" secondPos="2" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-40" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="2" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="40" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="3" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="19" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-8" driving="1" visible="1" active="1"/>
                    <Constraint type="11" first="4" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="50" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="1" firstPos="2" second="-1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="ExtrudeFeature" name="extrude" tag="Pad" base="-1" profile="0" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters lengthForward="0.80000001192092896" angleForward="0" lengthRev="10" angleRev="0" finalDir="0 1 0" dirType="3" extrudeType="0" addSubType="0"/>
        </Feature>
        <Feature type="PolarPatternFeature" name="PolarPattern" tag="PolarPattern" base="1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters mode="0" axisType="1" occurrences="8" reverse="0" angle="360" axisLocation="0 0 0" axisDirection="0 0 -1" originals="1"/>
        </Feature>
        <Feature type="SketcherFeature" name="sketcher" tag="Sketcher" base="-1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters>
                <Sketch origin="0 0 0" xAxis="1 0 0" yAxis="0 0 -1" normal="0 1 0" drawGrid="1" snapGrid="0">
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0 40 0" p2="0 -3 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0 -3 0" p2="6 -3 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="6 -3 0" p2="6 40 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="6 40 0" p2="0 40 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="7.2000000000000002 20 0" p2="7.2000000000000002 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="7.2000000000000002 0 0" p2="8 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="8 0 0" p2="8 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="8 20 0" p2="7.2000000000000002 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="14.000000190734863 20 0" p2="14.000000190734863 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="14.000000190734863 0 0" p2="14.800000190734863 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="14.800000190734863 0 0" p2="14.800000190734863 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="14.800000190734863 20 0" p2="14.000000190734863 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="22 20 0" p2="22 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="22 0 0" p2="21.199999999969535 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="21.199999999969535 0 0" p2="21.199999999969535 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="21.199999999969535 20 0" p2="22 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="31.199999999946748 20 0" p2="31.199999999946748 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="31.199999999946748 0 0" p2="32 0 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="32 0 0" p2="32 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="32 20 0" p2="31.199999999946748 20 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="40 20 0" p2="40 7.1054273576010019e-15 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="40 5.3290705182007514e-15 0" p2="42 5.3290705182007514e-15 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="42 20 0" p2="40 20 0"/>
                    <Geometry type="GeomArcOfCircle" construction="0" visible="1" center="13.715728752538096 10.000000000000009 0" radius="30" first="5.9433483977254644" last="6.623022216633708"/>
                    <Constraint type="1" first="0" firstPos="2" second="1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="1" firstPos="2" second="2" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="2" firstPos="2" second="3" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="3" firstPos="2" second="0" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="2" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="3" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="3" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-6" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="2" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="43" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="0" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-3" driving="1" visible="1" active="1"/>
                    <Constraint type="13" first="0" firstPos="1" second="-2" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="4" firstPos="2" second="5" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="5" firstPos="2" second="6" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="6" firstPos="2" second="7" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="7" firstPos="2" second="4" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="4" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="6" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="5" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="7" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="7" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-0.80000000000000004" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="4" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-20" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="5" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="8" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="5" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="8" firstPos="2" second="9" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="9" firstPos="2" second="10" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="10" firstPos="2" second="11" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="11" firstPos="2" second="8" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="8" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="10" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="9" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="11" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="11" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-0.80000000000000004" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="10" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="20" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="9" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="14.800000190734863" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="9" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="12" firstPos="2" second="13" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="13" firstPos="2" second="14" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="14" firstPos="2" second="15" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="15" firstPos="2" second="12" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="12" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="14" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="13" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="15" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="12" first="8" firstPos="0" second="12" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="12" first="11" firstPos="0" second="15" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="12" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="12" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="22" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="16" firstPos="2" second="17" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="17" firstPos="2" second="18" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="18" firstPos="2" second="19" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="19" firstPos="2" second="16" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="16" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="18" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="17" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="19" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="12" first="18" firstPos="0" second="12" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="12" first="13" firstPos="0" second="17" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="17" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="32" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="17" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="20" firstPos="2" second="21" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="22" firstPos="2" second="20" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="20" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="21" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="22" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="20" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-20" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="22" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-2" driving="1" visible="1" active="1"/>
                    <Constraint type="12" first="22" firstPos="0" second="21" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="22" firstPos="1" second="23" secondPos="2" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="21" firstPos="2" second="23" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="11" first="23" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="30" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="20" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="40" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="20" firstPos="2" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-0" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="RevolveFeature" name="Revolve" tag="Revolve" base="2" profile="3" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters origin="3 0 -18.5" angle="360" addSubType="0" axisType="1" reverse="0" axisLocation="0 0 0" axisDirection="0 0 -1"/>
        </Feature>
        <Feature type="SketcherFeature" name="sketcher" tag="Sketcher" base="4" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Face_126">
                    <Name>g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M10(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_6;:H:3,F;K7|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_7;:H:3,F;K8|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K9);FUS;:M(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M2(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K3);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M3(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K2);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M4(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K5|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_3;:H:3,F;K6);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M5(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K6|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K4);FUS);RFI;:H,F;:M2(g2v2;SKT;:H:4,V;:H,V;:H,V;:G;RVL;:L;RVL;:L;RVL;:H,F);FUS;:M(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M10(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_6;:H:3,F;K7|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_7;:H:3,F;K8|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K9);FUS;:M(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M2(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K3);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M3(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K2);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M4(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K5|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_3;:H:3,F;K6);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M5(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K6|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K4);FUS);RFI;:H,F;:M3;FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M10(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_6;:H:3,F;K7|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_7;:H:3,F;K8|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K9);FUS;:M(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M2(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K3);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M3(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K2);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M4(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K5|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_3;:H:3,F;K6);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M5(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K6|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K4);FUS);RFI;:H,F;:M4;FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M10(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_6;:H:3,F;K7|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_7;:H:3,F;K8|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K9);FUS;:M(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M2(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K3);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M3(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K2);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M4(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K5|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_3;:H:3,F;K6);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M5(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K6|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K4);FUS);RFI;:H,F;:M5;FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M10(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_6;:H:3,F;K7|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_7;:H:3,F;K8|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K9);FUS;:M(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M2(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K3);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M3(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K2);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M4(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K5|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_3;:H:3,F;K6);FUS|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;:M5(g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_2;:H:3,F;K6|g0;SKT;:H:4,E;:H,E;:G;XTR;:H,F;_8;:H:3,F;K4);FUS);RFI;:H,F;:M6;FUS);RFI</Name>
                </Sub>
            </References>
            <Parameters>
                <Sketch origin="-2.0516423291157318e-16 6.0797738784279916e-16 -40.000000000000114" xAxis="-1 0 0" yAxis="0 1 0" normal="-0 0 -1" drawGrid="1" snapGrid="0">
                    <Geometry type="GeomCircle" construction="0" visible="1" center="0 0 0" radius="4.5"/>
                    <Constraint type="18" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="9" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="ExtrudeFeature" name="extrude" tag="Pocket" base="4" profile="5" active="1">
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters lengthForward="51.343643188476562" angleForward="0" lengthRev="10" angleRev="0" finalDir="-0 -0 -1" dirType="3" extrudeType="0" addSubType="1"/>
        </Feature>
    </Body>
</MoonDocument>
