// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.DocObjects;
using Rhino.Geometry;
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace DatasmithRhino
{
	public static class FDatasmithRhinoUtilities
	{
		public static string GetMaterialHash(Material RhinoMaterial)
		{
			return ComputeHashStringFromBytes(SerializeMaterial(RhinoMaterial));
		}

		public static string GetTextureHash(Texture RhinoTexture)
		{
			string HashString;

			using (MemoryStream Stream = new MemoryStream())
			{
				using (BinaryWriter Writer = new BinaryWriter(Stream))
				{
					SerializeTexture(Writer, RhinoTexture);

				}

				HashString = ComputeHashStringFromBytes(Stream.ToArray());
			}

			return HashString;
		}

		private static string ComputeHashStringFromBytes(byte[] Data)
		{
			MD5 Md5Hash = MD5.Create();
			byte[] Hash = Md5Hash.ComputeHash(Data);

			StringBuilder Builder = new StringBuilder();
			Builder.Capacity = Hash.Length;
			for (int ByteIndex = 0, ByteCount = Hash.Length; ByteIndex < ByteCount; ++ByteIndex)
			{
				//Format the bytes hexadecimal characters.
				Builder.Append(Hash[ByteIndex].ToString("X2"));
			}

			return Builder.ToString();
		}

		private static byte[] SerializeMaterial(Material RhinoMaterial)
		{
			using (MemoryStream Stream = new MemoryStream())
			{
				using (BinaryWriter Writer = new BinaryWriter(Stream))
				{
					// Hash the material properties that are used to create the Unreal material: diffuse color, transparency, shininess and texture maps
					if (RhinoMaterial.Name != null)
					{
						Writer.Write(RhinoMaterial.Name);
					}

					Writer.Write(RhinoMaterial.DiffuseColor.R);
					Writer.Write(RhinoMaterial.DiffuseColor.G);
					Writer.Write(RhinoMaterial.DiffuseColor.B);
					Writer.Write(RhinoMaterial.DiffuseColor.A);

					Writer.Write(RhinoMaterial.Transparency);
					Writer.Write(RhinoMaterial.Shine);
					Writer.Write(RhinoMaterial.Reflectivity);

					Texture[] MaterialTextures = RhinoMaterial.GetTextures();
					for (int Index = 0; Index < MaterialTextures.Length; ++Index)
					{
						SerializeTexture(Writer, MaterialTextures[Index]);
					}
				}

				return Stream.ToArray();
			}
		}

		private static void SerializeTexture(BinaryWriter Writer, Texture RhinoTexture)
		{
			if (!RhinoTexture.Enabled || (RhinoTexture.TextureType != TextureType.Bitmap && RhinoTexture.TextureType != TextureType.Bump && RhinoTexture.TextureType != TextureType.Transparency))
			{
				return;
			}

			string FullPath = RhinoTexture.FileReference.FullPath;
			string FilePath;
			if (FullPath.Length != 0)
			{
				FilePath = FullPath;
			}
			else
			{
				string RelativePath = RhinoTexture.FileReference.RelativePath;
				if (RelativePath.Length == 0)
				{
					//No valid path found, skip the texture.
					return;
				}
				FilePath = RelativePath;
			}

			// Hash the parameters for texture maps
			Writer.Write(FilePath);
			Writer.Write((int)RhinoTexture.TextureType);
			Writer.Write(RhinoTexture.MappingChannelId);

			double Constant, A0, A1, A2, A3;
			RhinoTexture.GetAlphaBlendValues(out Constant, out A0, out A1, out A2, out A3);
			Writer.Write(Constant);
			Writer.Write(A0);
			Writer.Write(A1);
			Writer.Write(A2);
			Writer.Write(A3);

			SerializeTransform(Writer, RhinoTexture.UvwTransform);
		}

		private static void SerializeTransform(BinaryWriter Writer, Rhino.Geometry.Transform RhinoTransform)
		{
			Writer.Write(RhinoTransform.M00);
			Writer.Write(RhinoTransform.M01);
			Writer.Write(RhinoTransform.M02);
			Writer.Write(RhinoTransform.M03);
			Writer.Write(RhinoTransform.M10);
			Writer.Write(RhinoTransform.M11);
			Writer.Write(RhinoTransform.M12);
			Writer.Write(RhinoTransform.M13);
			Writer.Write(RhinoTransform.M20);
			Writer.Write(RhinoTransform.M21);
			Writer.Write(RhinoTransform.M22);
			Writer.Write(RhinoTransform.M23);
			Writer.Write(RhinoTransform.M30);
			Writer.Write(RhinoTransform.M31);
			Writer.Write(RhinoTransform.M32);
			Writer.Write(RhinoTransform.M33);
		}

		public static double RadianToDegree(double Radian)
		{
			return Radian * (180 / Math.PI);
		}

		public static Transform GetModelComponentTransform(ModelComponent RhinoModelComponent)
		{
			switch (RhinoModelComponent)
			{
				case InstanceObject InInstance:
					return InInstance.InstanceXform;

				case PointObject InPoint:
					Vector3d PointLocation = new Vector3d(InPoint.PointGeometry.Location);
					return Transform.Translation(PointLocation);

				case LightObject InLightObject:
					return GetLightTransform(InLightObject.LightGeometry);

				default:
					return Transform.Identity;
			}
		}

		public static Transform GetLightTransform(Light LightGeometry)
		{
			if (LightGeometry.IsLinearLight || LightGeometry.IsRectangularLight)
			{
				return GetAreaLightTransform(LightGeometry);
			}
			else
			{
				Transform RotationTransform = Transform.Rotation(new Vector3d(1, 0, 0), LightGeometry.Direction, new Point3d(0, 0, 0));
				Transform TranslationTransform = Transform.Translation(new Vector3d(LightGeometry.Location));

				return TranslationTransform * RotationTransform;
			}
		}

		private static Transform GetAreaLightTransform(Light LightGeometry)
		{
			Point3d Center = LightGeometry.Location + 0.5 * LightGeometry.Length;
			if (LightGeometry.IsRectangularLight)
			{
				Center += 0.5 * LightGeometry.Width;
			}

			Vector3d InvLengthAxis = -LightGeometry.Length;
			Vector3d WidthAxis = -LightGeometry.Width;
			Vector3d InvLightAxis = Vector3d.CrossProduct(WidthAxis, InvLengthAxis);
			InvLengthAxis.Unitize();
			WidthAxis.Unitize();
			InvLightAxis.Unitize();

			Transform RotationTransform = Transform.ChangeBasis(InvLightAxis, WidthAxis, InvLengthAxis, new Vector3d(1, 0, 0), new Vector3d(0, 1, 0), new Vector3d(0, 0, 1));
			Transform TranslationTransform = Transform.Translation(new Vector3d(Center));

			return TranslationTransform * RotationTransform;
		}

		public static string EvaluateAttributeUserText(RhinoSceneHierarchyNode InNode, string ValueFormula)
		{
			if (!ValueFormula.StartsWith("%<") || !ValueFormula.EndsWith(">%"))
			{
				// Nothing to evaluate, just return the string as-is.
				return ValueFormula;
			}

			RhinoObject NodeObject = InNode.Info.RhinoModelComponent as RhinoObject;
			RhinoObject ParentObject = null;
			RhinoSceneHierarchyNode CurrentNode = InNode;
			while (CurrentNode.LinkedNode != null)
			{
				CurrentNode = CurrentNode.LinkedNode;
				ParentObject = CurrentNode.Info.RhinoModelComponent as RhinoObject;
			}

			// In case this is an instance of a block sub-object, the ID held in the formula may not have been updated
			// with the definition object ID. We need to replace the ID with the definition object ID, otherwise the formula
			// will not evaluate correctly.
			if (InNode.LinkedNode != null && !InNode.LinkedNode.bIsRoot)
			{
				int IdStartIndex = ValueFormula.IndexOf("(\"") + 2;
				int IdEndIndex = ValueFormula.IndexOf("\")");

				if (IdStartIndex >= 0 && IdEndIndex > IdStartIndex)
				{
					ValueFormula = ValueFormula.Remove(IdStartIndex, IdEndIndex - IdStartIndex);
					ValueFormula = ValueFormula.Insert(IdStartIndex, NodeObject.Id.ToString());
				}
			}

			return RhinoApp.ParseTextField(ValueFormula, NodeObject, ParentObject);
		}

		public static string GetRhinoTextureFilePath(Texture RhinoTexture)
		{
			string FileName = "";
			string FilePath = RhinoTexture.FileReference.FullPath;
			if (FilePath != null && FilePath.Length != 0)
			{
				FileName = Path.GetFileName(FilePath);
				if (!File.Exists(FilePath))
				{
					FilePath = "";
				}
			}

			// Rhino's full path did not work, check with Rhino's relative path starting from current path
			string RhinoFilePath = RhinoDoc.ActiveDoc.Path;
			if (RhinoFilePath != null && RhinoFilePath != "")
			{
				string CurrentPath = Path.GetFullPath(Path.GetDirectoryName(RhinoFilePath));
				if (FilePath == null || FilePath.Length == 0)
				{
					string RelativePath = RhinoTexture.FileReference.RelativePath;
					if (RelativePath != null && RelativePath.Length != 0)
					{
						FilePath = Path.Combine(CurrentPath, RelativePath);
						FilePath = Path.GetFullPath(FilePath);

						if (!File.Exists(FilePath))
						{
							FilePath = "";
						}
					}
				}

				// Last resort, search for the file
				if (FilePath == null || FilePath == "")
				{
					// Search the texture in the CurrentPath and its sub-folders
					string[] FileNames = Directory.GetFiles(CurrentPath, FileName, SearchOption.AllDirectories);
					if (FileNames.Length > 0)
					{
						FilePath = Path.GetFullPath(FileNames[0]);
					}
				}
			}

			return FilePath;
		}

		public static bool GetRhinoTextureNameAndPath(Texture RhinoTexture, out string Name, out string TexturePath)
		{
			TexturePath = GetRhinoTextureFilePath(RhinoTexture);
			Name = "";

			if (string.IsNullOrEmpty(TexturePath))
			{
				return false;
			}

			Name = System.IO.Path.GetFileNameWithoutExtension(TexturePath);
			if (RhinoTexture.TextureType == TextureType.Bump)
			{
				Name += "_normal";
			}
			else if (RhinoTexture.TextureType == TextureType.Transparency)
			{
				Name += "_alpha";
			}

			return true;
		}
	}
}