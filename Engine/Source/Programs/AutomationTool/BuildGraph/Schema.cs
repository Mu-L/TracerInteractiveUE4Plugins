// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using System.Xml.Schema;
using System.Xml.Serialization;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool
{
	/// <summary>
	/// Information about a parameter to a task
	/// </summary>
	[DebuggerDisplay("{Name}")]
	class ScriptTaskParameter
	{
		/// <summary>
		/// Name of this parameter
		/// </summary>
		public string Name;

		/// <summary>
		/// Information about this field
		/// </summary>
		public FieldInfo FieldInfo;

		/// <summary>
		/// The type for values assigned to this field
		/// </summary>
		public Type ValueType;

		/// <summary>
		/// The ICollection interface for this type
		/// </summary>
		public Type CollectionType;

		/// <summary>
		/// Validation type for this field
		/// </summary>
		public TaskParameterValidationType ValidationType;

		/// <summary>
		/// Whether this parameter is optional
		/// </summary>
		public bool bOptional;

		/// <summary>
		/// Constructor
		/// </summary>
		public ScriptTaskParameter(string InName, FieldInfo InFieldInfo, TaskParameterValidationType InValidationType, bool bInOptional)
		{
			Name = InName;
			FieldInfo = InFieldInfo;
			ValueType = FieldInfo.FieldType;
			ValidationType = InValidationType;
			bOptional = bInOptional;

			if (ValueType.IsGenericType && ValueType.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				ValueType = ValueType.GetGenericArguments()[0];
				bOptional = true;
			}

			if(ValueType.IsClass)
			{
				foreach(Type InterfaceType in ValueType.GetInterfaces())
				{
					if(InterfaceType.IsGenericType)
					{
						Type GenericInterfaceType = InterfaceType.GetGenericTypeDefinition();
						if(GenericInterfaceType == typeof(ICollection<>))
						{
							CollectionType = InterfaceType;
							ValueType = InterfaceType.GetGenericArguments()[0];
						}
					}
				}
			}
		}
	}

	/// <summary>
	/// Helper class to serialize a task from an xml element
	/// </summary>
	[DebuggerDisplay("{Name}")]
	class ScriptTask
	{
		/// <summary>
		/// Name of this task
		/// </summary>
		public string Name;

		/// <summary>
		/// Type of the task to construct with this info
		/// </summary>
		public Type TaskClass;

		/// <summary>
		/// Type to construct with the parsed parameters
		/// </summary>
		public Type ParametersClass;

		/// <summary>
		/// Mapping of attribute name to field
		/// </summary>
		public Dictionary<string, ScriptTaskParameter> NameToParameter = new Dictionary<string,ScriptTaskParameter>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InName">Name of the task</param>
		/// <param name="InTaskClass">Task class to create</param>
		/// <param name="InParametersClass">Class type of an object to be constructed and passed as an argument to the task class constructor</param>
		public ScriptTask(string InName, Type InTaskClass, Type InParametersClass)
		{
			Name = InName;
			TaskClass = InTaskClass;
			ParametersClass = InParametersClass;

			// Find all the fields which are tagged as parameters in ParametersClass
			foreach(FieldInfo Field in ParametersClass.GetFields())
			{
				if(Field.MemberType == MemberTypes.Field)
				{
					TaskParameterAttribute ParameterAttribute = Field.GetCustomAttribute<TaskParameterAttribute>();
					if(ParameterAttribute != null)
					{
						NameToParameter.Add(Field.Name, new ScriptTaskParameter(Field.Name, Field, ParameterAttribute.ValidationType, ParameterAttribute.Optional));
					}
				}
			}
		}
	}

	/// <summary>
	/// Enumeration of standard types used in the schema. Avoids hard-coding names.
	/// </summary>
	enum ScriptSchemaStandardType
	{
		Graph,
		Trigger,
		TriggerBody,
		Agent,
		AgentBody,
		Node,
		NodeBody,
		Aggregate,
		Report,
		Badge,
		Label,
		Notify,
		Include,
		Option,
		EnvVar,
		Property,
		Regex,
		Macro,
		MacroBody,
		Expand,
		Trace,
		Warning,
		Error,
		Name,
		NameList,
		Tag,
		TagList,
		NameOrTag,
		NameOrTagList,
		QualifiedName,
		BalancedString,
		Boolean,
		Integer,
		LabelChange
	}

	/// <summary>
	/// Schema for build graph definitions. Stores information about the supported tasks, and allows validating an XML document.
	/// </summary>
	class ScriptSchema
	{
		/// <summary>
		/// Name of the root element
		/// </summary>
		public const string RootElementName = "BuildGraph";

		/// <summary>
		/// Namespace for the schema
		/// </summary>
		public const string NamespaceURI = "http://www.epicgames.com/BuildGraph";

		/// <summary>
		/// List of all the loaded classes which derive from BuildGraph.Task
		/// </summary>
		Dictionary<string, ScriptTask> NameToTask = new Dictionary<string, ScriptTask>();

		/// <summary>
		/// Qualified name for the string type
		/// </summary>
		static readonly XmlQualifiedName StringTypeName = new XmlQualifiedName("string", "http://www.w3.org/2001/XMLSchema");

		/// <summary>
		/// The inner xml schema
		/// </summary>
		public readonly XmlSchema CompiledSchema;

		/// <summary>
		/// Characters which are not permitted in names.
		/// </summary>
		public const string IllegalNameCharacters = "^<>:\"/\\|?*";

		/// <summary>
		/// Pattern which matches any name; alphanumeric characters, with single embedded spaces.
		/// </summary>
		const string NamePattern = "[^ " + IllegalNameCharacters +"]+( [^ " + IllegalNameCharacters + "]+)*";

		/// <summary>
		/// Pattern which matches a list of names, separated by semicolons.
		/// </summary>
		const string NameListPattern = NamePattern + "(;" + NamePattern + ")*";

		/// <summary>
		/// Pattern which matches any tag name; a name with a leading '#' character
		/// </summary>
		const string TagPattern = "#" + NamePattern;

		/// <summary>
		/// Pattern which matches a list of tag names, separated by semicolons;
		/// </summary>
		const string TagListPattern = TagPattern + "(;" + TagPattern + ")*";

		/// <summary>
		/// Pattern which matches any name or tag name; a name with a leading '#' character
		/// </summary>
		const string NameOrTagPattern = "#?" + NamePattern;

		/// <summary>
		/// Pattern which matches a list of names or tag names, separated by semicolons;
		/// </summary>
		const string NameOrTagListPattern = NameOrTagPattern + "(;" + NameOrTagPattern + ")*";

		/// <summary>
		/// Pattern which matches a qualified name.
		/// </summary>
		const string QualifiedNamePattern = NamePattern + "(\\." + NamePattern + ")*";

		/// <summary>
		/// Pattern which matches a property name
		/// </summary>
		const string PropertyPattern = "\\$\\(" + NamePattern + "\\)";

		/// <summary>
		/// Pattern which matches balanced parentheses in a string
		/// </summary>
		const string StringWithPropertiesPattern = "[^\\$]*" + "(" + "(" + PropertyPattern + "|" + "\\$[^\\(]" + ")" + "[^\\$]*" + ")+" + "\\$?";

		/// <summary>
		/// Pattern which matches balanced parentheses in a string
		/// </summary>
		const string BalancedStringPattern = "[^\\$]*" + "(" + "(" + PropertyPattern + "|" + "\\$[^\\(]" + ")" + "[^\\$]*" + ")*" + "\\$?";

		private ScriptSchema(XmlSchema Schema, Dictionary<string, ScriptTask> InNameToTask)
		{
			CompiledSchema = Schema;
			NameToTask = InNameToTask;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InNameToTask">Mapping of task name to information about how to construct it</param>
		public ScriptSchema(Dictionary<string, ScriptTask> InNameToTask)
		{
			NameToTask = InNameToTask;

			// Create a lookup from standard types to their qualified names
			Dictionary<Type, XmlQualifiedName> TypeToSchemaTypeName = new Dictionary<Type,XmlQualifiedName>();
			TypeToSchemaTypeName.Add(typeof(String), GetQualifiedTypeName(ScriptSchemaStandardType.BalancedString));
			TypeToSchemaTypeName.Add(typeof(Boolean), GetQualifiedTypeName(ScriptSchemaStandardType.Boolean));
			TypeToSchemaTypeName.Add(typeof(Int32), GetQualifiedTypeName(ScriptSchemaStandardType.Integer));
			TypeToSchemaTypeName.Add(typeof(FileReference), GetQualifiedTypeName(ScriptSchemaStandardType.BalancedString));
			TypeToSchemaTypeName.Add(typeof(DirectoryReference), GetQualifiedTypeName(ScriptSchemaStandardType.BalancedString));
			TypeToSchemaTypeName.Add(typeof(UnrealTargetPlatform), GetQualifiedTypeName(ScriptSchemaStandardType.BalancedString));

			// Create all the custom user types, and add them to the qualified name lookup
			List<XmlSchemaType> UserTypes = new List<XmlSchemaType>();
			foreach(Type Type in NameToTask.Values.SelectMany(x => x.NameToParameter.Values).Select(x => x.ValueType))
			{
				if(!TypeToSchemaTypeName.ContainsKey(Type))
				{
					if(Type.IsClass && Type.GetInterfaces().Any(x => x.GetGenericTypeDefinition() == typeof(ICollection<>)))
					{
						TypeToSchemaTypeName.Add(Type, GetQualifiedTypeName(ScriptSchemaStandardType.BalancedString));
					}
					else
					{
						string Name = Type.Name + "UserType";
						XmlSchemaType SchemaType = CreateUserType(Name, Type);
						UserTypes.Add(SchemaType);
						TypeToSchemaTypeName.Add(Type, new XmlQualifiedName(Name, NamespaceURI));
					}
				}
			}

			// Create all the task types
			Dictionary<string, XmlSchemaComplexType> TaskNameToType = new Dictionary<string,XmlSchemaComplexType>();
			foreach(ScriptTask Task in NameToTask.Values)
			{
				XmlSchemaComplexType TaskType = new XmlSchemaComplexType();
				TaskType.Name = Task.Name + "TaskType";
				foreach(ScriptTaskParameter Parameter in Task.NameToParameter.Values)
				{
					XmlQualifiedName SchemaTypeName = GetQualifiedTypeName(Parameter.ValidationType);
					if(SchemaTypeName == null)
					{
						SchemaTypeName = TypeToSchemaTypeName[Parameter.ValueType];
					}
					TaskType.Attributes.Add(CreateSchemaAttribute(Parameter.Name, SchemaTypeName, Parameter.bOptional? XmlSchemaUse.Optional : XmlSchemaUse.Required));
				}
				TaskType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
				TaskNameToType.Add(Task.Name, TaskType);
			}

			// Create the schema object
			XmlSchema NewSchema = new XmlSchema();
			NewSchema.TargetNamespace = NamespaceURI;
			NewSchema.ElementFormDefault = XmlSchemaForm.Qualified;
			NewSchema.Items.Add(CreateSchemaElement(RootElementName, ScriptSchemaStandardType.Graph));
			NewSchema.Items.Add(CreateGraphType());
			NewSchema.Items.Add(CreateTriggerType());
			NewSchema.Items.Add(CreateTriggerBodyType());
			NewSchema.Items.Add(CreateAgentType());
			NewSchema.Items.Add(CreateAgentBodyType());
			NewSchema.Items.Add(CreateNodeType());
			NewSchema.Items.Add(CreateNodeBodyType(TaskNameToType));
			NewSchema.Items.Add(CreateAggregateType());
			NewSchema.Items.Add(CreateReportType());
			NewSchema.Items.Add(CreateBadgeType());
			NewSchema.Items.Add(CreateLabelType());
			NewSchema.Items.Add(CreateEnumType(GetTypeName(ScriptSchemaStandardType.LabelChange), typeof(LabelChange)));
			NewSchema.Items.Add(CreateNotifyType());
			NewSchema.Items.Add(CreateIncludeType());
			NewSchema.Items.Add(CreateOptionType());
			NewSchema.Items.Add(CreateEnvVarType());
			NewSchema.Items.Add(CreatePropertyType());
			NewSchema.Items.Add(CreateRegexType());
			NewSchema.Items.Add(CreateMacroType());
			NewSchema.Items.Add(CreateMacroBodyType(TaskNameToType));
			NewSchema.Items.Add(CreateExpandType());
			NewSchema.Items.Add(CreateDiagnosticType(ScriptSchemaStandardType.Trace));
			NewSchema.Items.Add(CreateDiagnosticType(ScriptSchemaStandardType.Warning));
			NewSchema.Items.Add(CreateDiagnosticType(ScriptSchemaStandardType.Error));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.Name), "(" + NamePattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.NameList), "(" + NameListPattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.Tag), "(" + TagPattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.TagList), "(" + TagListPattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.NameOrTag), "(" + NameOrTagPattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.NameOrTagList), "(" + NameOrTagListPattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.QualifiedName), "(" + QualifiedNamePattern + "|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.BalancedString), BalancedStringPattern));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.Boolean), "(true|True|false|False|" + StringWithPropertiesPattern + ")"));
			NewSchema.Items.Add(CreateSimpleTypeFromRegex(GetTypeName(ScriptSchemaStandardType.Integer), "(" + "(-?[1-9][0-9]*|0)" + "|" + StringWithPropertiesPattern + ")"));
			foreach(XmlSchemaComplexType Type in TaskNameToType.Values)
			{
				NewSchema.Items.Add(Type);
			}
			foreach(XmlSchemaSimpleType Type in UserTypes)
			{
				NewSchema.Items.Add(Type);
			}

			// Now that we've finished, compile it and store it to the class
			XmlSchemaSet NewSchemaSet = new XmlSchemaSet();
			NewSchemaSet.Add(NewSchema);
			NewSchemaSet.Compile();
			foreach(XmlSchema NewCompiledSchema in NewSchemaSet.Schemas())
			{
				CompiledSchema = NewCompiledSchema;
			}
		}

		/// <summary>
		/// Gets information about the task with the given name
		/// </summary>
		/// <param name="TaskName">Name of the task</param>
		/// <param name="Task">Receives task info for the named task</param>
		/// <returns>True if the task name was found and Task is set, false otherwise.</returns>
		public bool TryGetTask(string TaskName, out ScriptTask Task)
		{
			return NameToTask.TryGetValue(TaskName, out Task);
		}

		/// <summary>
		/// Export the schema to a file
		/// </summary>
		/// <param name="File"></param>
		public void Export(FileReference File)
		{
			DirectoryReference.CreateDirectory(File.Directory);

			XmlWriterSettings Settings = new XmlWriterSettings();
			Settings.Indent = true;
			Settings.IndentChars = "  ";
			Settings.NewLineChars = "\n";

			using(XmlWriter Writer = XmlWriter.Create(File.FullName, Settings))
			{
				CompiledSchema.Write(Writer);
			}
		}

		/// <summary>
		/// Imports a schema from a file
		/// </summary>
		/// <param name="File">The XML file to import</param>
		/// <param name="NameToTask">Mapping of task name to information about how to construct it</param>
		/// <returns>A <see cref="ScriptSchema"/> deserialized from the XML file, or null if file doesn't exist</returns>
		public static ScriptSchema Import(FileReference File, Dictionary<string, ScriptTask> NameToTask)
		{
			if (!FileReference.Exists(File))
			{
				return null;
			}
			
			using (XmlReader SchemaFile = XmlReader.Create(File.FullName))
			{
				ScriptSchema ImportedSchema = new ScriptSchema(XmlSchema.Read(SchemaFile, ValidationCallback), NameToTask);
				return ImportedSchema;
			}
		}

		static void ValidationCallback(object Sender, ValidationEventArgs EventArgs)
		{
			if (EventArgs.Severity == XmlSeverityType.Warning)
			{
				Log.WriteLine(LogEventType.Warning, "WARNING: {0}", EventArgs.Message);
			}
			else if (EventArgs.Severity == XmlSeverityType.Error)
			{
				Log.WriteLine(LogEventType.Error, "ERROR: {0}", EventArgs.Message);
			}
		}

		/// <summary>
		/// Gets the bare name for the given script type
		/// </summary>
		/// <param name="Type">Script type to find the name of</param>
		/// <returns>Name of the schema type that matches the given script type</returns>
		static string GetTypeName(ScriptSchemaStandardType Type)
		{
			return Type.ToString() + "Type";
		}
		
		/// <summary>
		/// Gets the qualified name for the given script type
		/// </summary>
		/// <param name="Type">Script type to find the qualified name for</param>
		/// <returns>Qualified name of the schema type that matches the given script type</returns>
		static XmlQualifiedName GetQualifiedTypeName(ScriptSchemaStandardType Type)
		{
			return new XmlQualifiedName(GetTypeName(Type), NamespaceURI);
		}

		/// <summary>
		/// Gets the qualified name of the schema type for the given type of validation
		/// </summary>
		/// <returns>Qualified name for the corresponding schema type</returns>
		static XmlQualifiedName GetQualifiedTypeName(TaskParameterValidationType Type)
		{
			switch(Type)
			{
				case TaskParameterValidationType.TagList:
					return GetQualifiedTypeName(ScriptSchemaStandardType.TagList);
			}
			return null;
		}

		/// <summary>
		/// Creates the schema type representing the graph type
		/// </summary>
		/// <returns>Type definition for a graph</returns>
		static XmlSchemaType CreateGraphType()
		{
			XmlSchemaChoice GraphChoice = new XmlSchemaChoice();
			GraphChoice.MinOccurs = 0;
			GraphChoice.MaxOccursString = "unbounded";
			GraphChoice.Items.Add(CreateSchemaElement("Include", ScriptSchemaStandardType.Include));
			GraphChoice.Items.Add(CreateSchemaElement("Option", ScriptSchemaStandardType.Option));
			GraphChoice.Items.Add(CreateSchemaElement("EnvVar", ScriptSchemaStandardType.EnvVar));
			GraphChoice.Items.Add(CreateSchemaElement("Property", ScriptSchemaStandardType.Property));
			GraphChoice.Items.Add(CreateSchemaElement("Regex", ScriptSchemaStandardType.Regex));
			GraphChoice.Items.Add(CreateSchemaElement("Macro", ScriptSchemaStandardType.Macro));
			GraphChoice.Items.Add(CreateSchemaElement("Agent", ScriptSchemaStandardType.Agent));
			GraphChoice.Items.Add(CreateSchemaElement("Trigger", ScriptSchemaStandardType.Trigger));
			GraphChoice.Items.Add(CreateSchemaElement("Aggregate", ScriptSchemaStandardType.Aggregate));
			GraphChoice.Items.Add(CreateSchemaElement("Report", ScriptSchemaStandardType.Report));
			GraphChoice.Items.Add(CreateSchemaElement("Badge", ScriptSchemaStandardType.Badge));
			GraphChoice.Items.Add(CreateSchemaElement("Label", ScriptSchemaStandardType.Label));
			GraphChoice.Items.Add(CreateSchemaElement("Notify", ScriptSchemaStandardType.Notify));
			GraphChoice.Items.Add(CreateSchemaElement("Trace", ScriptSchemaStandardType.Trace));
			GraphChoice.Items.Add(CreateSchemaElement("Warning", ScriptSchemaStandardType.Warning));
			GraphChoice.Items.Add(CreateSchemaElement("Error", ScriptSchemaStandardType.Error));
			GraphChoice.Items.Add(CreateSchemaElement("Expand", ScriptSchemaStandardType.Expand));
			GraphChoice.Items.Add(CreateDoElement(ScriptSchemaStandardType.Graph));
			GraphChoice.Items.Add(CreateSwitchElement(ScriptSchemaStandardType.Graph));
			GraphChoice.Items.Add(CreateForEachElement(ScriptSchemaStandardType.Graph));

			XmlSchemaComplexType GraphType = new XmlSchemaComplexType();
			GraphType.Name = GetTypeName(ScriptSchemaStandardType.Graph);
			GraphType.Particle = GraphChoice;
			return GraphType;
		}

		/// <summary>
		/// Creates the schema type representing the trigger type
		/// </summary>
		/// <returns>Type definition for a trigger</returns>
		static XmlSchemaType CreateTriggerType()
		{
			XmlSchemaComplexContentExtension Extension = new XmlSchemaComplexContentExtension();
			Extension.BaseTypeName = GetQualifiedTypeName(ScriptSchemaStandardType.TriggerBody);
			Extension.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.QualifiedName, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaComplexContent ContentModel = new XmlSchemaComplexContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType ComplexType = new XmlSchemaComplexType();
			ComplexType.Name = GetTypeName(ScriptSchemaStandardType.Trigger);
			ComplexType.ContentModel = ContentModel;
			return ComplexType;
		}

		/// <summary>
		/// Creates the schema type representing the contents of a trigger type
		/// </summary>
		/// <returns>Type definition for an agent</returns>
		static XmlSchemaType CreateTriggerBodyType()
		{
			XmlSchemaChoice TriggerChoice = new XmlSchemaChoice();
			TriggerChoice.MinOccurs = 0;
			TriggerChoice.MaxOccursString = "unbounded";
			TriggerChoice.Items.Add(CreateSchemaElement("Property", ScriptSchemaStandardType.Property));
			TriggerChoice.Items.Add(CreateSchemaElement("Regex", ScriptSchemaStandardType.Regex));
			TriggerChoice.Items.Add(CreateSchemaElement("EnvVar", ScriptSchemaStandardType.EnvVar));
			TriggerChoice.Items.Add(CreateSchemaElement("Agent", ScriptSchemaStandardType.Agent));
			TriggerChoice.Items.Add(CreateSchemaElement("Aggregate", ScriptSchemaStandardType.Aggregate));
			TriggerChoice.Items.Add(CreateSchemaElement("Trace", ScriptSchemaStandardType.Trace));
			TriggerChoice.Items.Add(CreateSchemaElement("Warning", ScriptSchemaStandardType.Warning));
			TriggerChoice.Items.Add(CreateSchemaElement("Error", ScriptSchemaStandardType.Error));
			TriggerChoice.Items.Add(CreateSchemaElement("Expand", ScriptSchemaStandardType.Expand));
			TriggerChoice.Items.Add(CreateDoElement(ScriptSchemaStandardType.TriggerBody));
			TriggerChoice.Items.Add(CreateSwitchElement(ScriptSchemaStandardType.TriggerBody));
			TriggerChoice.Items.Add(CreateForEachElement(ScriptSchemaStandardType.TriggerBody));

			XmlSchemaComplexType TriggerType = new XmlSchemaComplexType();
			TriggerType.Name = GetTypeName(ScriptSchemaStandardType.TriggerBody);
			TriggerType.Particle = TriggerChoice;
			return TriggerType;
		}

		/// <summary>
		/// Creates the schema type representing the agent type
		/// </summary>
		/// <returns>Type definition for an agent</returns>
		static XmlSchemaType CreateAgentType()
		{
			XmlSchemaComplexContentExtension Extension = new XmlSchemaComplexContentExtension();
			Extension.BaseTypeName = GetQualifiedTypeName(ScriptSchemaStandardType.AgentBody);
			Extension.Attributes.Add(CreateSchemaAttribute("Name", StringTypeName, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Type", ScriptSchemaStandardType.NameList, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaComplexContent ContentModel = new XmlSchemaComplexContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType ComplexType = new XmlSchemaComplexType();
			ComplexType.Name = GetTypeName(ScriptSchemaStandardType.Agent);
			ComplexType.ContentModel = ContentModel;
			return ComplexType;
		}

		/// <summary>
		/// Creates the schema type representing the contents of agent type
		/// </summary>
		/// <returns>Type definition for an agent</returns>
		static XmlSchemaType CreateAgentBodyType()
		{
			XmlSchemaChoice AgentChoice = new XmlSchemaChoice();
			AgentChoice.MinOccurs = 0;
			AgentChoice.MaxOccursString = "unbounded";
			AgentChoice.Items.Add(CreateSchemaElement("Property", ScriptSchemaStandardType.Property));
			AgentChoice.Items.Add(CreateSchemaElement("Regex", ScriptSchemaStandardType.Regex));
			AgentChoice.Items.Add(CreateSchemaElement("EnvVar", ScriptSchemaStandardType.EnvVar));
			AgentChoice.Items.Add(CreateSchemaElement("Node", ScriptSchemaStandardType.Node));
			AgentChoice.Items.Add(CreateSchemaElement("Trace", ScriptSchemaStandardType.Trace));
			AgentChoice.Items.Add(CreateSchemaElement("Warning", ScriptSchemaStandardType.Warning));
			AgentChoice.Items.Add(CreateSchemaElement("Error", ScriptSchemaStandardType.Error));
			AgentChoice.Items.Add(CreateSchemaElement("Expand", ScriptSchemaStandardType.Expand));
			AgentChoice.Items.Add(CreateDoElement(ScriptSchemaStandardType.AgentBody));
			AgentChoice.Items.Add(CreateSwitchElement(ScriptSchemaStandardType.AgentBody));
			AgentChoice.Items.Add(CreateForEachElement(ScriptSchemaStandardType.AgentBody));

			XmlSchemaComplexType AgentType = new XmlSchemaComplexType();
			AgentType.Name = GetTypeName(ScriptSchemaStandardType.AgentBody);
			AgentType.Particle = AgentChoice;
			return AgentType;
		}

		/// <summary>
		/// Creates the schema type representing the node type
		/// </summary>
		/// <returns>Type definition for a node</returns>
		static XmlSchemaType CreateNodeType()
		{
			XmlSchemaComplexContentExtension Extension = new XmlSchemaComplexContentExtension();
			Extension.BaseTypeName = GetQualifiedTypeName(ScriptSchemaStandardType.NodeBody);
			Extension.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Requires", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("Produces", ScriptSchemaStandardType.TagList, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("After", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("Token", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("RunEarly", ScriptSchemaStandardType.Boolean, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("NotifyOnWarnings", ScriptSchemaStandardType.Boolean, XmlSchemaUse.Optional));

			XmlSchemaComplexContent ContentModel = new XmlSchemaComplexContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType ComplexType = new XmlSchemaComplexType();
			ComplexType.Name = GetTypeName(ScriptSchemaStandardType.Node);
			ComplexType.ContentModel = ContentModel;
			return ComplexType;
		}

		/// <summary>
		/// Creates the schema type representing the body of the node type
		/// </summary>
		/// <returns>Type definition for a node</returns>
		static XmlSchemaType CreateNodeBodyType(Dictionary<string, XmlSchemaComplexType> TaskNameToType)
		{
			XmlSchemaChoice NodeChoice = new XmlSchemaChoice();
			NodeChoice.MinOccurs = 0;
			NodeChoice.MaxOccursString = "unbounded";
			NodeChoice.Items.Add(CreateSchemaElement("Property", ScriptSchemaStandardType.Property));
			NodeChoice.Items.Add(CreateSchemaElement("Regex", ScriptSchemaStandardType.Regex));
			NodeChoice.Items.Add(CreateSchemaElement("EnvVar", ScriptSchemaStandardType.EnvVar));
			NodeChoice.Items.Add(CreateSchemaElement("Trace", ScriptSchemaStandardType.Trace));
			NodeChoice.Items.Add(CreateSchemaElement("Warning", ScriptSchemaStandardType.Warning));
			NodeChoice.Items.Add(CreateSchemaElement("Error", ScriptSchemaStandardType.Error));
			NodeChoice.Items.Add(CreateSchemaElement("Expand", ScriptSchemaStandardType.Expand));
			NodeChoice.Items.Add(CreateDoElement(ScriptSchemaStandardType.NodeBody));
			NodeChoice.Items.Add(CreateSwitchElement(ScriptSchemaStandardType.NodeBody));
			NodeChoice.Items.Add(CreateForEachElement(ScriptSchemaStandardType.NodeBody));
			foreach (KeyValuePair<string, XmlSchemaComplexType> Pair in TaskNameToType.OrderBy(x => x.Key))
			{
				NodeChoice.Items.Add(CreateSchemaElement(Pair.Key, new XmlQualifiedName(Pair.Value.Name, NamespaceURI)));
			}

			XmlSchemaComplexType NodeType = new XmlSchemaComplexType();
			NodeType.Name = GetTypeName(ScriptSchemaStandardType.NodeBody);
			NodeType.Particle = NodeChoice;
			return NodeType;
		}

		/// <summary>
		/// Creates the schema type representing the aggregate type
		/// </summary>
		/// <returns>Type definition for an aggregate</returns>
		static XmlSchemaType CreateAggregateType()
		{
			XmlSchemaComplexType AggregateType = new XmlSchemaComplexType();
			AggregateType.Name = GetTypeName(ScriptSchemaStandardType.Aggregate);
			AggregateType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Label", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Requires", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Required));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Include", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Exclude", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return AggregateType;
		}

		/// <summary>
		/// Creates the schema type representing the report type
		/// </summary>
		/// <returns>Type definition for a report</returns>
		static XmlSchemaType CreateReportType()
		{
			XmlSchemaComplexType ReportType = new XmlSchemaComplexType();
			ReportType.Name = GetTypeName(ScriptSchemaStandardType.Report);
			ReportType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			ReportType.Attributes.Add(CreateSchemaAttribute("Requires", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Required));
			ReportType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return ReportType;
		}

		/// <summary>
		/// Creates the schema type representing the badge type
		/// </summary>
		/// <returns>Type definition for a badge</returns>
		static XmlSchemaType CreateBadgeType()
		{
			XmlSchemaComplexType BadgeType = new XmlSchemaComplexType();
			BadgeType.Name = GetTypeName(ScriptSchemaStandardType.Badge);
			BadgeType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			BadgeType.Attributes.Add(CreateSchemaAttribute("Requires", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			BadgeType.Attributes.Add(CreateSchemaAttribute("Targets", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			BadgeType.Attributes.Add(CreateSchemaAttribute("Project", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));
			BadgeType.Attributes.Add(CreateSchemaAttribute("Change", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			BadgeType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return BadgeType;
		}

		/// <summary>
		/// Creates the schema type representing the label type
		/// </summary>
		/// <returns>Type definition for a label</returns>
		static XmlSchemaType CreateLabelType()
		{
			XmlSchemaComplexType LabelType = new XmlSchemaComplexType();
			LabelType.Name = GetTypeName(ScriptSchemaStandardType.Label);
			LabelType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("Category", ScriptSchemaStandardType.Name, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("UgsBadge", ScriptSchemaStandardType.Name, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("UgsProject", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("Change", ScriptSchemaStandardType.LabelChange, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("Requires", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Required));
			LabelType.Attributes.Add(CreateSchemaAttribute("Include", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("Exclude", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			LabelType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return LabelType;
		}

		/// <summary>
		/// Creates the schema type representing a notifier
		/// </summary>
		/// <returns>Type definition for a notifier</returns>
		static XmlSchemaType CreateNotifyType()
		{
			XmlSchemaComplexType AggregateType = new XmlSchemaComplexType();
			AggregateType.Name = GetTypeName(ScriptSchemaStandardType.Notify);
			AggregateType.Attributes.Add(CreateSchemaAttribute("Targets", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Except", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Nodes", ScriptSchemaStandardType.NameOrTagList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Triggers", ScriptSchemaStandardType.NameList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Reports", ScriptSchemaStandardType.NameList, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Users", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Submitters", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Warnings", ScriptSchemaStandardType.Boolean, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			AggregateType.Attributes.Add(CreateSchemaAttribute("Absolute", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return AggregateType;
		}

		/// <summary>
		/// Creates the schema type representing an include type
		/// </summary>
		/// <returns>Type definition for an include directive</returns>
		static XmlSchemaType CreateIncludeType()
		{
			XmlSchemaComplexType PropertyType = new XmlSchemaComplexType();
			PropertyType.Name = GetTypeName(ScriptSchemaStandardType.Include);
			PropertyType.Attributes.Add(CreateSchemaAttribute("Script", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));
			PropertyType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return PropertyType;
		}

		/// <summary>
		/// Creates the schema type representing a parameter type
		/// </summary>
		/// <returns>Type definition for a parameter</returns>
		static XmlSchemaType CreateOptionType()
		{
			XmlSchemaComplexType OptionType = new XmlSchemaComplexType();
			OptionType.Name = GetTypeName(ScriptSchemaStandardType.Option);
			OptionType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			OptionType.Attributes.Add(CreateSchemaAttribute("Restrict", StringTypeName, XmlSchemaUse.Optional));
			OptionType.Attributes.Add(CreateSchemaAttribute("DefaultValue", StringTypeName, XmlSchemaUse.Required));
			OptionType.Attributes.Add(CreateSchemaAttribute("Description", StringTypeName, XmlSchemaUse.Required));
			OptionType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return OptionType;
		}

		/// <summary>
		/// Creates the schema type representing a environment variable type
		/// </summary>
		/// <returns>Type definition for an environment variable property</returns>
		static XmlSchemaType CreateEnvVarType()
		{
			XmlSchemaComplexType EnvVarType = new XmlSchemaComplexType();
			EnvVarType.Name = GetTypeName(ScriptSchemaStandardType.EnvVar);
			EnvVarType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			EnvVarType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return EnvVarType;
		}

		/// <summary>
		/// Creates the schema type representing a property type
		/// </summary>
		/// <returns>Type definition for a property</returns>
		static XmlSchemaType CreatePropertyType()
		{
			XmlSchemaSimpleContentExtension Extension = new XmlSchemaSimpleContentExtension();
			Extension.BaseTypeName = StringTypeName;
			Extension.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Value", StringTypeName, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaSimpleContent ContentModel = new XmlSchemaSimpleContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType PropertyType = new XmlSchemaComplexType();
			PropertyType.Name = GetTypeName(ScriptSchemaStandardType.Property);
			PropertyType.ContentModel = ContentModel;
			return PropertyType;
		}

		/// <summary>
		/// Creates the schema type representing a regex type
		/// </summary>
		/// <returns>Type definition for a regex</returns>
		static XmlSchemaType CreateRegexType()
		{
			XmlSchemaSimpleContentExtension Extension = new XmlSchemaSimpleContentExtension();
			Extension.BaseTypeName = StringTypeName;

			Extension.Attributes.Add(CreateSchemaAttribute("Input", StringTypeName, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Pattern", StringTypeName, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Capture", ScriptSchemaStandardType.NameList, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaSimpleContent ContentModel = new XmlSchemaSimpleContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType RegexType  = new XmlSchemaComplexType();
			RegexType.Name = GetTypeName(ScriptSchemaStandardType.Regex);
			RegexType.ContentModel = ContentModel;
			return RegexType;
		}

		/// <summary>
		/// Creates the schema type representing the macro type
		/// </summary>
		/// <returns>Type definition for a node</returns>
		static XmlSchemaType CreateMacroType()
		{
			XmlSchemaComplexContentExtension Extension = new XmlSchemaComplexContentExtension();
			Extension.BaseTypeName = GetQualifiedTypeName(ScriptSchemaStandardType.MacroBody);
			Extension.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.Name, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Arguments", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("OptionalArguments", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaComplexContent ContentModel = new XmlSchemaComplexContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType ComplexType = new XmlSchemaComplexType();
			ComplexType.Name = GetTypeName(ScriptSchemaStandardType.Macro);
			ComplexType.ContentModel = ContentModel;
			return ComplexType;
		}

		/// <summary>
		/// Creates the schema type representing the macro type
		/// </summary>
		/// <returns>Type definition for a node</returns>
		static XmlSchemaType CreateMacroBodyType(Dictionary<string, XmlSchemaComplexType> TaskNameToType)
		{
			XmlSchemaChoice MacroChoice = new XmlSchemaChoice();
			MacroChoice.MinOccurs = 0;
			MacroChoice.MaxOccursString = "unbounded";

			// Graph scope
			MacroChoice.Items.Add(CreateSchemaElement("Include", ScriptSchemaStandardType.Include));
			MacroChoice.Items.Add(CreateSchemaElement("Option", ScriptSchemaStandardType.Option));
			MacroChoice.Items.Add(CreateSchemaElement("EnvVar", ScriptSchemaStandardType.EnvVar));
			MacroChoice.Items.Add(CreateSchemaElement("Property", ScriptSchemaStandardType.Property));
			MacroChoice.Items.Add(CreateSchemaElement("Regex", ScriptSchemaStandardType.Regex));
			MacroChoice.Items.Add(CreateSchemaElement("Macro", ScriptSchemaStandardType.Macro));
			MacroChoice.Items.Add(CreateSchemaElement("Agent", ScriptSchemaStandardType.Agent));
			MacroChoice.Items.Add(CreateSchemaElement("Trigger", ScriptSchemaStandardType.Trigger));
			MacroChoice.Items.Add(CreateSchemaElement("Aggregate", ScriptSchemaStandardType.Aggregate));
			MacroChoice.Items.Add(CreateSchemaElement("Report", ScriptSchemaStandardType.Report));
			MacroChoice.Items.Add(CreateSchemaElement("Badge", ScriptSchemaStandardType.Badge));
			MacroChoice.Items.Add(CreateSchemaElement("Notify", ScriptSchemaStandardType.Notify));
			MacroChoice.Items.Add(CreateSchemaElement("Trace", ScriptSchemaStandardType.Trace));
			MacroChoice.Items.Add(CreateSchemaElement("Warning", ScriptSchemaStandardType.Warning));
			MacroChoice.Items.Add(CreateSchemaElement("Error", ScriptSchemaStandardType.Error));
			MacroChoice.Items.Add(CreateSchemaElement("Expand", ScriptSchemaStandardType.Expand));

			// Agent scope
			MacroChoice.Items.Add(CreateSchemaElement("Node", ScriptSchemaStandardType.Node));

			// Node scope
			MacroChoice.Items.Add(CreateDoElement(ScriptSchemaStandardType.NodeBody));
			MacroChoice.Items.Add(CreateSwitchElement(ScriptSchemaStandardType.NodeBody));
			MacroChoice.Items.Add(CreateForEachElement(ScriptSchemaStandardType.NodeBody));
			foreach (KeyValuePair<string, XmlSchemaComplexType> Pair in TaskNameToType.OrderBy(x => x.Key))
			{
				MacroChoice.Items.Add(CreateSchemaElement(Pair.Key, new XmlQualifiedName(Pair.Value.Name, NamespaceURI)));
			}

			XmlSchemaComplexType NodeType = new XmlSchemaComplexType();
			NodeType.Name = GetTypeName(ScriptSchemaStandardType.MacroBody);
			NodeType.Particle = MacroChoice;
			return NodeType;
		}

		/// <summary>
		/// Creates the schema type representing a macro expansion
		/// </summary>
		/// <returns>Type definition for expanding a macro</returns>
		static XmlSchemaType CreateExpandType()
		{
			XmlSchemaAnyAttribute AnyAttribute = new XmlSchemaAnyAttribute();
			AnyAttribute.ProcessContents = XmlSchemaContentProcessing.Skip;

			XmlSchemaComplexType PropertyType = new XmlSchemaComplexType();
			PropertyType.Name = GetTypeName(ScriptSchemaStandardType.Expand);
			PropertyType.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));
			PropertyType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			PropertyType.AnyAttribute = AnyAttribute;
			return PropertyType;
		}

		/// <summary>
		/// Creates the schema type representing a warning or error type
		/// </summary>
		/// <returns>Type definition for a warning</returns>
		static XmlSchemaType CreateDiagnosticType(ScriptSchemaStandardType StandardType)
		{
			XmlSchemaComplexType PropertyType = new XmlSchemaComplexType();
			PropertyType.Name = GetTypeName(StandardType);
			PropertyType.Attributes.Add(CreateSchemaAttribute("Message", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));
			PropertyType.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			return PropertyType;
		}

		/// <summary>
		/// Creates an element representing a conditional "Do" block, which recursively contains another type
		/// </summary>
		/// <param name="InnerType">The base type for the do block to contain</param>
		/// <returns>New schema element for the block</returns>
		static XmlSchemaElement CreateDoElement(ScriptSchemaStandardType InnerType)
		{
			XmlSchemaComplexContentExtension Extension = new XmlSchemaComplexContentExtension();
			Extension.BaseTypeName = GetQualifiedTypeName(InnerType);
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaComplexContent ContentModel = new XmlSchemaComplexContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType SchemaType = new XmlSchemaComplexType();
			SchemaType.ContentModel = ContentModel;

			XmlSchemaElement Element = new XmlSchemaElement();
			Element.Name = "Do";
			Element.SchemaType = SchemaType;
			return Element;
		}

		/// <summary>
		/// Creates an element representing a conditional "Switch" block, which recursively contains another type
		/// </summary>
		/// <param name="InnerType">The base type for the do block to contain</param>
		/// <returns>New schema element for the block</returns>
		static XmlSchemaElement CreateSwitchElement(ScriptSchemaStandardType InnerType)
		{
			// Create the "Option" element
			XmlSchemaComplexContentExtension CaseExtension = new XmlSchemaComplexContentExtension();
			CaseExtension.BaseTypeName = GetQualifiedTypeName(InnerType);
			CaseExtension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));

			XmlSchemaComplexContent CaseContentModel = new XmlSchemaComplexContent();
			CaseContentModel.Content = CaseExtension;

			XmlSchemaComplexType CaseSchemaType = new XmlSchemaComplexType();
			CaseSchemaType.ContentModel = CaseContentModel;

			XmlSchemaElement CaseElement = new XmlSchemaElement();
			CaseElement.Name = "Case";
			CaseElement.SchemaType = CaseSchemaType;
			CaseElement.MinOccurs = 0;
			CaseElement.MaxOccursString = "unbounded";

			// Create the "Otherwise" element
			XmlSchemaElement OtherwiseElement = new XmlSchemaElement();
			OtherwiseElement.Name = "Default";
			OtherwiseElement.SchemaTypeName = GetQualifiedTypeName(InnerType);
			OtherwiseElement.MinOccurs = 0;
			OtherwiseElement.MaxOccurs = 1;

			// Create the "Switch" element
			XmlSchemaSequence SwitchSequence = new XmlSchemaSequence();
			SwitchSequence.Items.Add(CaseElement);
			SwitchSequence.Items.Add(OtherwiseElement);

			XmlSchemaComplexType SwitchSchemaType = new XmlSchemaComplexType();
			SwitchSchemaType.Particle = SwitchSequence;

			XmlSchemaElement SwitchElement = new XmlSchemaElement();
			SwitchElement.Name = "Switch";
			SwitchElement.SchemaType = SwitchSchemaType;
			return SwitchElement;
		}

		/// <summary>
		/// Creates an element representing a conditional "ForEach" block, which recursively contains another type
		/// </summary>
		/// <param name="InnerType">The base type for the foreach block to contain</param>
		/// <returns>New schema element for the block</returns>
		static XmlSchemaElement CreateForEachElement(ScriptSchemaStandardType InnerType)
		{
			XmlSchemaComplexContentExtension Extension = new XmlSchemaComplexContentExtension();
			Extension.BaseTypeName = GetQualifiedTypeName(InnerType);
			Extension.Attributes.Add(CreateSchemaAttribute("Name", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Values", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Required));
			Extension.Attributes.Add(CreateSchemaAttribute("Separator", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));
			Extension.Attributes.Add(CreateSchemaAttribute("If", ScriptSchemaStandardType.BalancedString, XmlSchemaUse.Optional));

			XmlSchemaComplexContent ContentModel = new XmlSchemaComplexContent();
			ContentModel.Content = Extension;

			XmlSchemaComplexType SchemaType = new XmlSchemaComplexType();
			SchemaType.ContentModel = ContentModel;

			XmlSchemaElement Element = new XmlSchemaElement();
			Element.Name = "ForEach";
			Element.SchemaType = SchemaType;
			return Element;
		}

		/// <summary>
		/// Constructs an XmlSchemaElement and initializes it with the given parameters
		/// </summary>
		/// <param name="Name">Element name</param>
		/// <param name="SchemaType">Type enumeration for the attribute</param>
		/// <returns>A new XmlSchemaElement object</returns>
		static XmlSchemaElement CreateSchemaElement(string Name, ScriptSchemaStandardType SchemaType)
		{
			return CreateSchemaElement(Name, GetQualifiedTypeName(SchemaType));
		}

		/// <summary>
		/// Constructs an XmlSchemaElement and initializes it with the given parameters
		/// </summary>
		/// <param name="Name">Element name</param>
		/// <param name="SchemaTypeName">Qualified name of the type for this element</param>
		/// <returns>A new XmlSchemaElement object</returns>
		static XmlSchemaElement CreateSchemaElement(string Name, XmlQualifiedName SchemaTypeName)
		{
			XmlSchemaElement Element = new XmlSchemaElement();
			Element.Name = Name;
			Element.SchemaTypeName = SchemaTypeName;
			return Element;
		}

		/// <summary>
		/// Constructs an XmlSchemaAttribute and initialize it with the given parameters
		/// </summary>
		/// <param name="Name">The attribute name</param>
		/// <param name="SchemaType">Type enumeration for the attribute</param>
		/// <param name="Use">Whether the attribute is required or optional</param>
		/// <returns>A new XmlSchemaAttribute object</returns>
		static XmlSchemaAttribute CreateSchemaAttribute(string Name, ScriptSchemaStandardType SchemaType, XmlSchemaUse Use)
		{
			return CreateSchemaAttribute(Name, GetQualifiedTypeName(SchemaType), Use);
		}

		/// <summary>
		/// Constructs an XmlSchemaAttribute and initialize it with the given parameters
		/// </summary>
		/// <param name="Name">The attribute name</param>
		/// <param name="SchemaTypeName">Qualified name of the type for this attribute</param>
		/// <param name="Use">Whether the attribute is required or optional</param>
		/// <returns>The new attribute</returns>
		static XmlSchemaAttribute CreateSchemaAttribute(string Name, XmlQualifiedName SchemaTypeName, XmlSchemaUse Use)
		{
			XmlSchemaAttribute Attribute = new XmlSchemaAttribute();
			Attribute.Name = Name;
			Attribute.SchemaTypeName = SchemaTypeName;
			Attribute.Use = Use;
			return Attribute;
		}

		/// <summary>
		/// Creates a simple type that is the union of two other types
		/// </summary>
		/// <param name="Name">The name of the type</param>
		/// <param name="ValidTypes">List of valid types for the union</param>
		/// <returns>A simple type which will match the given pattern</returns>
		static XmlSchemaSimpleType CreateSimpleTypeFromUnion(string Name, params XmlSchemaType[] ValidTypes)
		{
			XmlSchemaSimpleTypeUnion Union = new XmlSchemaSimpleTypeUnion();
			foreach (XmlSchemaType ValidType in ValidTypes)
			{
				Union.BaseTypes.Add(ValidType);
			}

			XmlSchemaSimpleType UnionType = new XmlSchemaSimpleType();
			UnionType.Name = Name;
			UnionType.Content = Union;
			return UnionType;
		}

		/// <summary>
		/// Creates a simple type that matches a regex
		/// </summary>
		/// <param name="Name">Name of the new type</param>
		/// <param name="Pattern">Regex pattern to match</param>
		/// <returns>A simple type which will match the given pattern</returns>
		static XmlSchemaSimpleType CreateSimpleTypeFromRegex(string Name, string Pattern)
		{
			XmlSchemaPatternFacet PatternFacet = new XmlSchemaPatternFacet();
			PatternFacet.Value = Pattern;

			XmlSchemaSimpleTypeRestriction Restriction = new XmlSchemaSimpleTypeRestriction();
			Restriction.BaseTypeName = StringTypeName;
			Restriction.Facets.Add(PatternFacet);

			XmlSchemaSimpleType SimpleType = new XmlSchemaSimpleType();
			SimpleType.Name = Name;
			SimpleType.Content = Restriction;
			return SimpleType;
		}

		/// <summary>
		/// Create a schema type for the given user type. Currently only handles enumerations.
		/// </summary>
		/// <param name="Name">Name for the new type</param>
		/// <param name="Type">CLR type information to create a schema type for</param>
		static XmlSchemaType CreateUserType(string Name, Type Type)
		{
			if(Type.IsEnum)
			{
				return CreateSimpleTypeFromUnion(Name, CreateEnumType(null, Type), CreateSimpleTypeFromRegex(null, StringWithPropertiesPattern));
			}
			else
			{
				throw new AutomationException("Cannot create custom type in schema for '{0}'", Type.Name);
			}
		}

		/// <summary>
		/// Create a schema type for the given enum.
		/// </summary>
		/// <param name="Name">Name for the new type</param>
		/// <param name="Type">CLR type information to create a schema type for</param>
		static XmlSchemaType CreateEnumType(string Name, Type Type)
		{
			XmlSchemaSimpleTypeRestriction Restriction = new XmlSchemaSimpleTypeRestriction();
			Restriction.BaseTypeName = StringTypeName;

			foreach(string EnumName in Enum.GetNames(Type))
			{
				XmlSchemaEnumerationFacet Facet = new XmlSchemaEnumerationFacet();
				Facet.Value = EnumName;
				Restriction.Facets.Add(Facet);
			}

			XmlSchemaSimpleType SchemaType = new XmlSchemaSimpleType();
			SchemaType.Name = Name;
			SchemaType.Content = Restriction;
			return SchemaType;
		}
	}
}
