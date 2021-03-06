// Copyright Epic Games, Inc. All Rights Reserved.

//------------------------------------------------------------------------------
// <auto-generated />
//
// This file was automatically generated by SWIG (http://www.swig.org).
// Version 4.0.1
//
// Do not make changes to this file unless you know what you are doing--modify
// the SWIG interface file instead.
//------------------------------------------------------------------------------


public class FDatasmithFacadeSpotLight : FDatasmithFacadePointLight {
  private global::System.Runtime.InteropServices.HandleRef swigCPtr;

  internal FDatasmithFacadeSpotLight(global::System.IntPtr cPtr, bool cMemoryOwn) : base(DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeSpotLight_SWIGUpcast(cPtr), cMemoryOwn) {
    swigCPtr = new global::System.Runtime.InteropServices.HandleRef(this, cPtr);
  }

  internal static global::System.Runtime.InteropServices.HandleRef getCPtr(FDatasmithFacadeSpotLight obj) {
    return (obj == null) ? new global::System.Runtime.InteropServices.HandleRef(null, global::System.IntPtr.Zero) : obj.swigCPtr;
  }

  protected override void Dispose(bool disposing) {
    lock(this) {
      if (swigCPtr.Handle != global::System.IntPtr.Zero) {
        if (swigCMemOwn) {
          swigCMemOwn = false;
          DatasmithFacadeCSharpPINVOKE.delete_FDatasmithFacadeSpotLight(swigCPtr);
        }
        swigCPtr = new global::System.Runtime.InteropServices.HandleRef(null, global::System.IntPtr.Zero);
      }
      base.Dispose(disposing);
    }
  }

  public FDatasmithFacadeSpotLight(string InElementName) : this(DatasmithFacadeCSharpPINVOKE.new_FDatasmithFacadeSpotLight(InElementName), true) {
  }

  public float GetInnerConeAngle() {
    float ret = DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeSpotLight_GetInnerConeAngle(swigCPtr);
    return ret;
  }

  public void SetInnerConeAngle(float InnerConeAngle) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeSpotLight_SetInnerConeAngle(swigCPtr, InnerConeAngle);
  }

  public float GetOuterConeAngle() {
    float ret = DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeSpotLight_GetOuterConeAngle(swigCPtr);
    return ret;
  }

  public void SetOuterConeAngle(float OuterConeAngle) {
    DatasmithFacadeCSharpPINVOKE.FDatasmithFacadeSpotLight_SetOuterConeAngle(swigCPtr, OuterConeAngle);
  }

}
