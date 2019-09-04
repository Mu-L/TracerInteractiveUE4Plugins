﻿// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace nDisplayLauncher.Cluster.Config
{
	public enum ConfigurationVersion
	{
		Ver21, // 4.21 and older
		Ver22,
		Ver23
	}

	public static class ConfigurationVersionHelpers
	{
		public static ConfigurationVersion FromString(string Ver)
		{
			if (Ver.CompareTo("23") == 0)
			{
				return ConfigurationVersion.Ver23;
			}
			if (Ver.CompareTo("22") == 0)
			{
				return ConfigurationVersion.Ver22;
			}
			else
			{
				return ConfigurationVersion.Ver21;
			}
		}

		public static string ToString(ConfigurationVersion Ver)
		{
			switch (Ver)
			{
				case ConfigurationVersion.Ver23:
					return "23";

				case ConfigurationVersion.Ver22:
					return "22";

				default:
					return "21";
			}
		}
	}


}
