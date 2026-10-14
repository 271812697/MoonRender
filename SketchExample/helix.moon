<MoonDocument version="1">
    <Body name="Body">
        <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
        <Feature type="DatumLineFeature" name="DatumLine" tag="DatumLine" base="-1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
                <Actor name="Edges"/>
                <Actor name="Edge_0"/>
                <Actor name="Vertices"/>
                <Actor name="Vertex_0"/>
                <Actor name="Vertex_1"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters origin="0 0 0" direction="0 0 1" length="10"/>
        </Feature>
        <Feature type="SketcherFeature" name="sketcher" tag="Sketcher" base="-1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
                <Actor name="Edges"/>
                <Actor name="Edge_0"/>
                <Actor name="Edge_1"/>
                <Actor name="Edge_2"/>
                <Actor name="Edge_3"/>
                <Actor name="Vertices"/>
                <Actor name="Vertex_0"/>
                <Actor name="Vertex_1"/>
                <Actor name="Vertex_2"/>
                <Actor name="Vertex_3"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters>
                <Sketch origin="0 0 0" xAxis="1 0 0" yAxis="0 0 -1" normal="0 1 0" drawGrid="1" snapGrid="0">
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0.95000000000000018 0.98999999999999999 0" p2="0.95000000000000018 -0.84000000000000008 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0.95000000000000018 -0.83999999999999997 0" p2="3.9100000000000001 -0.83999999999999997 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="3.9100000000000001 -0.83999999999999997 0" p2="3.9100000000000001 0.9900000000000001 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="3.9100000000000001 0.98999999999999999 0" p2="0.95000000000000018 0.98999999999999999 0"/>
                    <Constraint type="1" first="0" firstPos="2" second="1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="1" firstPos="2" second="2" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="2" firstPos="2" second="3" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="3" firstPos="2" second="0" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="2" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="2" first="3" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="HelixFeature" name="Helix" tag="Helix" base="-1" profile="1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
                <Actor name="Solid_0"/>
                <Actor name="Shell_0"/>
                <Actor name="Faces"/>
                <Actor name="Face_0"/>
                <Actor name="Face_1"/>
                <Actor name="Face_2"/>
                <Actor name="Face_3"/>
                <Actor name="Face_4"/>
                <Actor name="Face_5"/>
                <Actor name="Edges"/>
                <Actor name="Edge_0"/>
                <Actor name="Edge_1"/>
                <Actor name="Edge_2"/>
                <Actor name="Edge_3"/>
                <Actor name="Edge_4"/>
                <Actor name="Edge_5"/>
                <Actor name="Edge_6"/>
                <Actor name="Edge_7"/>
                <Actor name="Edge_8"/>
                <Actor name="Edge_9"/>
                <Actor name="Edge_10"/>
                <Actor name="Edge_11"/>
                <Actor name="Vertices"/>
                <Actor name="Vertex_0"/>
                <Actor name="Vertex_1"/>
                <Actor name="Vertex_2"/>
                <Actor name="Vertex_3"/>
                <Actor name="Vertex_4"/>
                <Actor name="Vertex_5"/>
                <Actor name="Vertex_6"/>
                <Actor name="Vertex_7"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters addSubType="0" axisMode="Reference" mode="pitch_height_angle" pitch="10.079999923706055" height="30" turns="2.9761904987167971" angle="14.899999618530273" growth="2.6820805288922109" leftHanded="0" reversed="0" axis="0" axisBase="0 0 -5" axisDir="0 0 1"/>
        </Feature>
        <Feature type="ExtrudeFeature" name="extrude" tag="Pad" base="2" profile="-1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
                <Actor name="Solid_0"/>
                <Actor name="Shell_0"/>
                <Actor name="Faces"/>
                <Actor name="Face_0"/>
                <Actor name="Face_1"/>
                <Actor name="Face_2"/>
                <Actor name="Face_3"/>
                <Actor name="Face_4"/>
                <Actor name="Face_5"/>
                <Actor name="Face_6"/>
                <Actor name="Face_7"/>
                <Actor name="Face_8"/>
                <Actor name="Face_9"/>
                <Actor name="Edges"/>
                <Actor name="Edge_0"/>
                <Actor name="Edge_1"/>
                <Actor name="Edge_2"/>
                <Actor name="Edge_3"/>
                <Actor name="Edge_4"/>
                <Actor name="Edge_5"/>
                <Actor name="Edge_6"/>
                <Actor name="Edge_7"/>
                <Actor name="Edge_8"/>
                <Actor name="Edge_9"/>
                <Actor name="Edge_10"/>
                <Actor name="Edge_11"/>
                <Actor name="Edge_12"/>
                <Actor name="Edge_13"/>
                <Actor name="Edge_14"/>
                <Actor name="Edge_15"/>
                <Actor name="Edge_16"/>
                <Actor name="Edge_17"/>
                <Actor name="Edge_18"/>
                <Actor name="Edge_19"/>
                <Actor name="Vertices"/>
                <Actor name="Vertex_0"/>
                <Actor name="Vertex_1"/>
                <Actor name="Vertex_2"/>
                <Actor name="Vertex_3"/>
                <Actor name="Vertex_4"/>
                <Actor name="Vertex_5"/>
                <Actor name="Vertex_6"/>
                <Actor name="Vertex_7"/>
                <Actor name="Vertex_8"/>
                <Actor name="Vertex_9"/>
                <Actor name="Vertex_10"/>
                <Actor name="Vertex_11"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Face_5"/>
            </References>
            <Parameters lengthForward="23.123739242553711" angleForward="0" lengthRev="10" angleRev="0" finalDir="0.14898687258287704 0.98883915158839397 -0.00020996170813842956" dirType="0" extrudeType="0" addSubType="0"/>
        </Feature>
        <Feature type="FilletFeature" name="Fillet" tag="Fillet" base="3" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
                <Actor name="Solid_0"/>
                <Actor name="Shell_0"/>
                <Actor name="Faces"/>
                <Actor name="Face_0"/>
                <Actor name="Face_1"/>
                <Actor name="Face_2"/>
                <Actor name="Face_3"/>
                <Actor name="Face_4"/>
                <Actor name="Face_5"/>
                <Actor name="Face_6"/>
                <Actor name="Face_7"/>
                <Actor name="Face_8"/>
                <Actor name="Face_9"/>
                <Actor name="Face_10"/>
                <Actor name="Edges"/>
                <Actor name="Edge_0"/>
                <Actor name="Edge_1"/>
                <Actor name="Edge_2"/>
                <Actor name="Edge_3"/>
                <Actor name="Edge_4"/>
                <Actor name="Edge_5"/>
                <Actor name="Edge_6"/>
                <Actor name="Edge_7"/>
                <Actor name="Edge_8"/>
                <Actor name="Edge_9"/>
                <Actor name="Edge_10"/>
                <Actor name="Edge_11"/>
                <Actor name="Edge_12"/>
                <Actor name="Edge_13"/>
                <Actor name="Edge_14"/>
                <Actor name="Edge_15"/>
                <Actor name="Edge_16"/>
                <Actor name="Edge_17"/>
                <Actor name="Edge_18"/>
                <Actor name="Edge_19"/>
                <Actor name="Edge_20"/>
                <Actor name="Edge_21"/>
                <Actor name="Edge_22"/>
                <Actor name="Edge_23"/>
                <Actor name="Edge_24"/>
                <Actor name="Vertices"/>
                <Actor name="Vertex_0"/>
                <Actor name="Vertex_1"/>
                <Actor name="Vertex_2"/>
                <Actor name="Vertex_3"/>
                <Actor name="Vertex_4"/>
                <Actor name="Vertex_5"/>
                <Actor name="Vertex_6"/>
                <Actor name="Vertex_7"/>
                <Actor name="Vertex_8"/>
                <Actor name="Vertex_9"/>
                <Actor name="Vertex_10"/>
                <Actor name="Vertex_11"/>
                <Actor name="Vertex_12"/>
                <Actor name="Vertex_13"/>
                <Actor name="Vertex_14"/>
                <Actor name="Vertex_15"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Edge_10">
                    <Name>Edge3;:H,E;:M;FUS;:H,E</Name>
                </Sub>
            </References>
            <Parameters radius="1.3906968832015991" len="0.5106847882270813" useAllEdges="0" toolSubtractive="1" origin1="10.139520645141602 -12.157417297363281 29.926362991333008" origin2="10.139520645141602 -12.157417297363281 29.926362991333008" dir1="-0.7948983907699585 -0.60674256086349487 0.00015159808390308172" dir2="0.14898687601089478 0.98883914947509766 -0.00020996171224396676"/>
        </Feature>
        <Feature type="FilletFeature" name="Fillet" tag="Fillet" base="4" active="1">
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Edge_18">
                    <Name>Edge1;:H,E;:M;FUS;:H,E;:H,E;:H,E</Name>
                </Sub>
            </References>
            <Parameters radius="1" len="0.5106847882270813" useAllEdges="0" toolSubtractive="0" origin1="7.2125563621520996 -11.716415405273438 29.926366806030273" origin2="7.2125563621520996 -11.716415405273438 29.926366806030273" dir1="-0.87680351734161377 -0.48084884881973267 0.0001281315489904955" dir2="0.14898687601089478 0.98883914947509766 -0.00020996171224396676"/>
        </Feature>
    </Body>
</MoonDocument>
