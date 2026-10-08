<MoonDocument version="1">
    <Body name="Body">
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
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="-22 -7.1054273576010019e-15 0" p2="0 -7.1054273576010019e-15 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="0 -7.1054273576010019e-15 0" p2="2.7554552992026191e-15 45 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="2.7554552992026191e-15 45 0" p2="-30 45.000000000000021 0"/>
                    <Geometry type="GeomArcOfCircle" construction="0" visible="1" center="112.98295136707841 47.208080243036214 0" radius="143" first="3.1570343878360867" last="3.4780301702485561"/>
                    <Constraint type="2" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="10" first="0" firstPos="2" second="1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="10" first="1" firstPos="2" second="2" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="0" firstPos="2" second="-1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="45" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="2" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="-30" driving="1" visible="1" active="1"/>
                    <Constraint type="6" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="22" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="2" firstPos="2" second="3" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="0" firstPos="1" second="3" secondPos="2" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="11" first="3" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="143" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="RevolveFeature" name="Revolve" tag="Revolve" base="-1" profile="0" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References/>
            <Parameters origin="-13.725529670715332 0 -23.595188140869141" angle="360" addSubType="0" axisType="1" reverse="0" axisLocation="0 0 0" axisDirection="0 0 -1"/>
        </Feature>
        <Feature type="FilletFeature" name="Fillet" tag="Fillet" base="1" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Edge_0">
                    <Name>g0v1;SKT;:H:4,V;:H,V;:G;RVL;:H,E</Name>
                </Sub>
            </References>
            <Parameters radius="4" len="0.96046864986419678" useAllEdges="0" toolSubtractive="1" origin1="22 2.6942229168686917e-15 3.5527136788005009e-15" origin2="22 2.6942229168686917e-15 3.5527136788005009e-15" dir1="-1 -1.2246468525851679e-16 -0" dir2="0.33012643456459045 4.0428829632888552e-17 -0.94393670558929443"/>
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
                    <Geometry type="GeomArcOfCircle" construction="1" visible="1" center="-112.98295136707841 47.208080243036569 0" radius="143" first="5.9670986286051608" last="6.2677435729332904"/>
                    <Geometry type="GeomArcOfCircle" construction="0" visible="1" center="33 28.000000000000014 0" radius="12" first="4.9948431657856052" last="8.1228023751603295"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="36.344561066093036 16.475508198832614 0" p2="31.819298667383709 15.162216758853905 0"/>
                    <Geometry type="GeomArcOfCircle" construction="0" visible="1" center="36 0.75660200739465422 0" radius="15" first="1.8532505121958123" last="2.3488297802316143"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="25.471799765567287 11.441029920188628 0" p2="25.115652168474156 11.0900899123742 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" p1="29.812863406047665 39.569017258673718 0" p2="28.655961680180273 39.25030359927856 0"/>
                    <Geometry type="GeomArcOfCircle" construction="0" visible="1" external="1" center="112.98295136707841 47.208080243036214 0" radius="143" first="3.1570343878360867" last="3.4576793321642163"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" external="1" p1="-22.932706607513637 2.7566020073946547 0" p2="22.932706607513524 2.7566020073946542 0"/>
                    <Geometry type="GeomLineSegment" construction="0" visible="1" external="1" p1="-30 45.000000000000014 0" p2="30 45.000000000000014 0"/>
                    <Constraint type="1" first="0" firstPos="2" second="-3" secondPos="2" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="1" first="0" firstPos="1" second="-4" secondPos="2" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="11" first="0" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="143" driving="1" visible="1" active="1"/>
                    <Constraint type="11" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="12" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="1" firstPos="3" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="33" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="-3" firstPos="2" second="1" secondPos="3" third="-2000" thirdPos="0" value="-17" driving="1" visible="1" active="1"/>
                    <Constraint type="13" first="1" firstPos="2" second="0" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="5" first="1" firstPos="1" second="2" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="5" first="2" firstPos="2" second="3" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="11" first="3" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="15" driving="1" visible="1" active="1"/>
                    <Constraint type="7" first="3" firstPos="3" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="36" driving="1" visible="1" active="1"/>
                    <Constraint type="8" first="0" firstPos="1" second="3" secondPos="3" third="-2000" thirdPos="0" value="-2" driving="1" visible="1" active="1"/>
                    <Constraint type="13" first="3" firstPos="2" second="0" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="5" first="3" firstPos="2" second="4" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="5" first="1" firstPos="2" second="5" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="6" first="4" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0.5" driving="1" visible="1" active="1"/>
                    <Constraint type="6" first="5" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="1.2" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="DatumPlaneFeature" name="DatumPlane" tag="DatumPlane" base="3" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Edge_4">
                    <Name>g5;SKT;:H:4,E;:H,E</Name>
                </Sub>
            </References>
            <Parameters mapMode="NormalToEdge" automaticSize="1" offset="0 0 0" rotation="0" length="21.052820205688477" width="21.052820205688477" origin="29.812864303588867 0 -39.569015502929688" normal="-0.96408486366271973 0 0.26559475064277649" xAxis="0.26559475064277649 0 0.96408486366271973"/>
        </Feature>
        <Feature type="SketcherFeature" name="sketcher" tag="Sketcher" base="4" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Face_0"/>
            </References>
            <Parameters>
                <Sketch origin="29.812864303588867 0 -39.569015502929688" xAxis="-0.26559472462610612 0 -0.9640847692245651" yAxis="-0 -1 0" normal="-0.9640847692245651 0 0.26559472462610612" drawGrid="1" snapGrid="0">
                    <Geometry type="GeomEllipse" construction="0" visible="1" center="-1.5 1.100933720171737e-17 0" major="3.0000000000000009" minor="1.5" majorDir="0 1 0"/>
                    <Geometry type="GeomLineSegment" construction="1" visible="1" p1="-1.5 3 0" p2="-1.5 -3 0"/>
                    <Geometry type="GeomLineSegment" construction="1" visible="1" p1="-3 2.201866116854494e-17 0" p2="0 0 0"/>
                    <Geometry type="GeomPoint" construction="1" visible="1" p="-1.5 2.5980762113533169 0"/>
                    <Geometry type="GeomPoint" construction="1" visible="1" p="-1.5 -2.5980762113533165 0"/>
                    <Constraint type="15" first="1" firstPos="0" second="0" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1" alignmentType="1" alignmentIndex="-1"/>
                    <Constraint type="15" first="2" firstPos="0" second="0" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1" alignmentType="2" alignmentIndex="-1"/>
                    <Constraint type="15" first="3" firstPos="1" second="0" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1" alignmentType="3" alignmentIndex="-1"/>
                    <Constraint type="15" first="4" firstPos="1" second="0" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1" alignmentType="4" alignmentIndex="-1"/>
                    <Constraint type="1" first="2" firstPos="2" second="-1" secondPos="1" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="3" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="0" driving="1" visible="1" active="1"/>
                    <Constraint type="6" first="2" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="3" driving="1" visible="1" active="1"/>
                    <Constraint type="6" first="1" firstPos="0" second="-2000" secondPos="0" third="-2000" thirdPos="0" value="6" driving="1" visible="1" active="1"/>
                </Sketch>
            </Parameters>
        </Feature>
        <Feature type="ThicknessFeature" name="Thickness" tag="Thickness" base="2" active="0">
            <Hidden>
                <Actor name="AllFaces"/>
                <Actor name="AllEdges"/>
                <Actor name="AllVertices"/>
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Face_3">
                    <Name>g2v2;SKT;:H:4,V;:H,V;:G;RVL;:L;RVL;:L;RVL;:H,F;:H,F;:H,F</Name>
                </Sub>
            </References>
            <Parameters thickNessValue="3" scale="0.84852814674377441" mode="0" joinType="0" reverse="1" intersection="0" dir="1 1.2246468525851679e-16 0" midPoint="30 3.6739401871785891e-15 -45"/>
        </Feature>
        <Feature type="PipeFeature" name="Pipe" tag="Pipe" base="6" profile="5" spine="3" active="0">
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
            </Hidden>
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Edge_3">
                    <Name>g1;SKT;:H:4,E;:H,E</Name>
                </Sub>
                <Sub value="Edge_4">
                    <Name>g5;SKT;:H:4,E;:H,E</Name>
                </Sub>
                <Sub value="Edge_2">
                    <Name>g2;SKT;:H:4,E;:H,E</Name>
                </Sub>
                <Sub value="Edge_1">
                    <Name>g3;SKT;:H:4,E;:H,E</Name>
                </Sub>
                <Sub value="Edge_0">
                    <Name>g4;SKT;:H:4,E;:H,E</Name>
                </Sub>
            </References>
            <Parameters addSubType="0" mode="Standard" transition="Transformed" binormal="0 0 1"/>
        </Feature>
        <Feature type="FilletFeature" name="Fillet" tag="Fillet" base="7" active="1">
            <Pose pos="0 0 0" rot="0 0 0 1" scale="1 1 1"/>
            <References>
                <Sub value="Edge_6">
                    <Name>g3;SKT;:H:4,E;:H,E;:G;RVL;:H,F;:M;FLT;:H,F;:H,F;:H,F;:H,F;:G3;FUS;:H,E</Name>
                </Sub>
                <Sub value="Edge_5">
                    <Name>g3;SKT;:H:4,E;:H,E;:G;RVL;:H,F;:M;FLT;:H,F;:H,F;:H,F;:H,F;:G4;FUS;:H,E</Name>
                </Sub>
            </References>
            <Parameters radius="1" len="1.0606601238250732" useAllEdges="0" toolSubtractive="0" origin1="25.888301849365234 2.9999411106109619 -13.807033538818359" origin2="25.888301849365234 2.9999411106109619 -13.807033538818359" dir1="-0.10693849623203278 0.99368584156036377 -0.033950075507164001" dir2="0.97174733877182007 0.0097920168191194534 0.23582035303115845"/>
        </Feature>
    </Body>
</MoonDocument>
