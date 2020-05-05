﻿// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// Exception thrown for errors parsing JSON files
	/// </summary>
	public class JsonParseException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Format">Format string</param>
		/// <param name="Args">Optional arguments</param>
		public JsonParseException(string Format, params object[] Args)
			: base(String.Format(Format, Args))
		{
		}
	}

	/// <summary>
	/// Stores a JSON object in memory
	/// </summary>
	public class JsonObject
	{
		Dictionary<string, object> RawObject;

		/// <summary>
		/// Construct a JSON object from the raw string -> object dictionary
		/// </summary>
		/// <param name="InRawObject">Raw object parsed from disk</param>
		public JsonObject(Dictionary<string, object> InRawObject)
		{
			RawObject = new Dictionary<string, object>(InRawObject, StringComparer.InvariantCultureIgnoreCase);
		}

		/// <summary>
		/// Read a JSON file from disk and construct a JsonObject from it
		/// </summary>
		/// <param name="File">File to read from</param>
		/// <returns>New JsonObject instance</returns>
		public static JsonObject Read(FileReference File)
		{
			string Text = FileReference.ReadAllText(File);
			try
			{
				return Parse(Text);
			}
			catch(Exception Ex)
			{
				throw new JsonParseException("Unable to parse {0}: {1}", File, Ex.Message);
			}
		}

		/// <summary>
		/// Tries to read a JSON file from disk
		/// </summary>
		/// <param name="FileName">File to read from</param>
		/// <param name="Result">On success, receives the parsed object</param>
		/// <returns>True if the file was read, false otherwise</returns>
		public static bool TryRead(FileReference FileName, out JsonObject Result)
		{
			if (!FileReference.Exists(FileName))
			{
				Result = null;
				return false;
			}

			string Text = FileReference.ReadAllText(FileName);
			return TryParse(Text, out Result);
		}

		/// <summary>
		/// Parse a JsonObject from the given raw text string
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <returns>New JsonObject instance</returns>
		public static JsonObject Parse(string Text)
		{
			Dictionary<string, object> CaseSensitiveRawObject = (Dictionary<string, object>)fastJSON.JSON.Instance.Parse(Text);
			return new JsonObject(CaseSensitiveRawObject);
		}

		/// <summary>
		/// Try to parse a JsonObject from the given raw text string
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <param name="Result">On success, receives the new JsonObject</param>
		/// <returns>True if the object was parsed</returns>
		public static bool TryParse(string Text, out JsonObject Result)
		{
			try
			{
				Result = Parse(Text);
				return true;
			}
			catch (Exception)
			{
				Result = null;
				return false;
			}
		}

		/// <summary>
		/// List of key names in this object
		/// </summary>
		public IEnumerable<string> KeyNames
		{
			get { return RawObject.Keys; }
		}

		/// <summary>
		/// Gets a string field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public string GetStringField(string FieldName)
		{
			string StringValue;
			if (!TryGetStringField(FieldName, out StringValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return StringValue;
		}

		/// <summary>
		/// Tries to read a string field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetStringField(string FieldName, out string Result)
		{
			object RawValue;
			if (RawObject.TryGetValue(FieldName, out RawValue) && (RawValue is string))
			{
				Result = (string)RawValue;
				return true;
			}
			else
			{
				Result = null;
				return false;
			}
		}

		/// <summary>
		/// Gets a string array field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public string[] GetStringArrayField(string FieldName)
		{
			string[] StringValues;
			if (!TryGetStringArrayField(FieldName, out StringValues))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return StringValues;
		}

		/// <summary>
		/// Tries to read a string array field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetStringArrayField(string FieldName, out string[] Result)
		{
			object RawValue;
			if (RawObject.TryGetValue(FieldName, out RawValue) && (RawValue is IEnumerable<object>) && ((IEnumerable<object>)RawValue).All(x => x is string))
			{
				Result = ((IEnumerable<object>)RawValue).Select(x => (string)x).ToArray();
				return true;
			}
			else
			{
				Result = null;
				return false;
			}
		}

		/// <summary>
		/// Gets a boolean field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public bool GetBoolField(string FieldName)
		{
			bool BoolValue;
			if (!TryGetBoolField(FieldName, out BoolValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return BoolValue;
		}

		/// <summary>
		/// Tries to read a bool field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetBoolField(string FieldName, out bool Result)
		{
			object RawValue;
			if (RawObject.TryGetValue(FieldName, out RawValue) && (RawValue is Boolean))
			{
				Result = (bool)RawValue;
				return true;
			}
			else
			{
				Result = false;
				return false;
			}
		}

		/// <summary>
		/// Gets an integer field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public int GetIntegerField(string FieldName)
		{
			int IntegerValue;
			if (!TryGetIntegerField(FieldName, out IntegerValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return IntegerValue;
		}

		/// <summary>
		/// Tries to read an integer field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetIntegerField(string FieldName, out int Result)
		{
			object RawValue;
			if (!RawObject.TryGetValue(FieldName, out RawValue) || !int.TryParse(RawValue.ToString(), out Result))
			{
				Result = 0;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Tries to read an unsigned integer field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetUnsignedIntegerField(string FieldName, out uint Result)
		{
			object RawValue;
			if (!RawObject.TryGetValue(FieldName, out RawValue) || !uint.TryParse(RawValue.ToString(), out Result))
			{
				Result = 0;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Gets a double field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public double GetDoubleField(string FieldName)
		{
			double DoubleValue;
			if (!TryGetDoubleField(FieldName, out DoubleValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return DoubleValue;
		}

		/// <summary>
		/// Tries to read a double field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetDoubleField(string FieldName, out double Result)
		{
			object RawValue;
			if (!RawObject.TryGetValue(FieldName, out RawValue) || !double.TryParse(RawValue.ToString(), out Result))
			{
				Result = 0.0;
				return false;
			}
			return true;
		}

		/// <summary>
		/// Gets an enum field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public T GetEnumField<T>(string FieldName) where T : struct
		{
			T EnumValue;
			if (!TryGetEnumField(FieldName, out EnumValue))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return EnumValue;
		}

		/// <summary>
		/// Tries to read an enum field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetEnumField<T>(string FieldName, out T Result) where T : struct
		{
			string StringValue;
			if (!TryGetStringField(FieldName, out StringValue) || !Enum.TryParse<T>(StringValue, true, out Result))
			{
				Result = default(T);
				return false;
			}
			return true;
		}

		/// <summary>
		/// Tries to read an enum array field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetEnumArrayField<T>(string FieldName, out T[] Result) where T : struct
		{
			string[] StringValues;
			if (!TryGetStringArrayField(FieldName, out StringValues))
			{
				Result = null;
				return false;
			}

			T[] EnumValues = new T[StringValues.Length];
			for (int Idx = 0; Idx < StringValues.Length; Idx++)
			{
				if (!Enum.TryParse<T>(StringValues[Idx], true, out EnumValues[Idx]))
				{
					Result = null;
					return false;
				}
			}

			Result = EnumValues;
			return true;
		}

		/// <summary>
		/// Gets an object field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public JsonObject GetObjectField(string FieldName)
		{
			JsonObject Result;
			if (!TryGetObjectField(FieldName, out Result))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return Result;
		}

		/// <summary>
		/// Tries to read an object field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetObjectField(string FieldName, out JsonObject Result)
		{
			object RawValue;
			if (RawObject.TryGetValue(FieldName, out RawValue) && (RawValue is Dictionary<string, object>))
			{
				Result = new JsonObject((Dictionary<string, object>)RawValue);
				return true;
			}
			else
			{
				Result = null;
				return false;
			}
		}

		/// <summary>
		/// Gets an object array field by the given name from the object, throwing an exception if it is not there or cannot be parsed.
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <returns>The field value</returns>
		public JsonObject[] GetObjectArrayField(string FieldName)
		{
			JsonObject[] Result;
			if (!TryGetObjectArrayField(FieldName, out Result))
			{
				throw new JsonParseException("Missing or invalid '{0}' field", FieldName);
			}
			return Result;
		}

		/// <summary>
		/// Tries to read an object array field by the given name from the object
		/// </summary>
		/// <param name="FieldName">Name of the field to get</param>
		/// <param name="Result">On success, receives the field value</param>
		/// <returns>True if the field could be read, false otherwise</returns>
		public bool TryGetObjectArrayField(string FieldName, out JsonObject[] Result)
		{
			object RawValue;
			if (RawObject.TryGetValue(FieldName, out RawValue) && (RawValue is IEnumerable<object>) && ((IEnumerable<object>)RawValue).All(x => x is Dictionary<string, object>))
			{
				Result = ((IEnumerable<object>)RawValue).Select(x => new JsonObject((Dictionary<string, object>)x)).ToArray();
				return true;
			}
			else
			{
				Result = null;
				return false;
			}
		}
	}
}
