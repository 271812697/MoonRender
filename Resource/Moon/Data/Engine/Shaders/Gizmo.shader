// Gizmo axes: the coloured X/Y/Z rings-and-arrows gizmo, drawn as three instances.
//
// A material MoonMatc consumes (see docs/ShaderPipeline.md). Both stages own their main():
// the vertex stage rebuilds the instance matrix per gl_InstanceID, scales the mesh by the
// distance to the camera and writes gl_Position itself, and the fragment stage writes its own
// single output. The generated transform and skeleton cannot express any of that, so MoonMatc
// takes the two blocks verbatim and only puts the prelude and the declared parameters in
// front of them - no attribute declarations, no VS_OUT, no FRAGMENT_COLOR, no main.

material {
    name         : Gizmo,
    shadingModel : unlit,

    // The legacy gizmo pass drew with no blending (the fragment stage writes alpha 1.0), no
    // culling, and depth writes on.
    blending     : opaque,
    culling      : none,
    depthWrite   : true,

    // u_IsPickable was declared in both stages, though only the vertex stage reads it; one
    // entry covers both, which is exactly what the two copies amounted to. None of the three
    // is set from C++ today, so they keep the GL defaults (false / 0) they had before.
    parameters : [
        { type : bool, name : u_IsBall },
        { type : bool, name : u_IsPickable },
        { type : int,  name : u_HighlightedAxis }
    ]
}

vertex {
    // Kept with the legacy attribute layout even though only geo_Pos is read: the instance
    // buffer binds all three locations, and dropping one would shift the others.
    layout (location = 0) in vec3 geo_Pos;
    layout (location = 1) in vec2 geo_TexCoords;
    layout (location = 2) in vec3 geo_Normal;

    out VS_OUT
    {
        vec3 Color;
    } vs_out;

    mat4 rotationMatrix(vec3 axis, float angle)
    {
        axis = normalize(axis);
        float s = sin(angle);
        float c = cos(angle);
        float oc = 1.0 - c;
        
        return mat4
        (
            oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,  0.0,
            oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,  0.0,
            oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c,           0.0,
            0.0,                                0.0,                                0.0,                                1.0
        );
    }

    void main()
    {
        mat4 instanceModel = ubo_Model;

        if (gl_InstanceID == 1) 
            instanceModel *= rotationMatrix(vec3(0, 1, 0), radians(-90)); /* X axis */
        else if (gl_InstanceID == 2) 
            instanceModel *= rotationMatrix(vec3(1, 0, 0), radians(90)); /* Y axis */

        float distanceToCamera = distance(ubo_ViewPos, instanceModel[3].xyz);

    	vec3 pos = geo_Pos;

        vec3 fragPos = vec3(instanceModel * vec4(pos * distanceToCamera * 0.1f, 1.0));

    	if (u_IsPickable)
    	{
    		int blueComponent = 0;

    		if (gl_InstanceID == 1)
    			blueComponent = 252;

    		if (gl_InstanceID == 2)
    			blueComponent = 253;

    		if (gl_InstanceID == 0)
    			blueComponent = 254;

    		vs_out.Color = vec3(1.0f, 1.0f, blueComponent / 255.0f);
    	}
    	else
    	{
    		if (u_IsBall)
    		{
    			vs_out.Color = vec3(1.0f);
    		}
    		else
    		{
    			float red	= float(gl_InstanceID == 1); // X
    			float green = float(gl_InstanceID == 2); // Y
    			float blue	= float(gl_InstanceID == 0); // Z

    			if (!u_IsPickable && ((gl_InstanceID == 1 && u_HighlightedAxis == 0) || (gl_InstanceID == 2 && u_HighlightedAxis == 1) || (gl_InstanceID == 0 && u_HighlightedAxis == 2)))
    			{
    				vs_out.Color = vec3(1.0f, 1.0f, 0.0f);
    			}	
    			else
    			{
    				vs_out.Color = vec3(red, green, blue);
    			}
    		}
    	}

        gl_Position = ubo_Projection * ubo_View * vec4(fragPos, 1.0);
    }
}

fragment {
    out vec4 FRAGMENT_COLOR;

    in VS_OUT
    {
        vec3 Color;
    } fs_in;

    void main()
    {
    	FRAGMENT_COLOR = vec4(fs_in.Color, 1.0f);
    }
}
