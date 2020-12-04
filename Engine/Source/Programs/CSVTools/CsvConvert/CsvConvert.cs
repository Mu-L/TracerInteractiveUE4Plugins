// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using CSVStats;

namespace CSVInfo
{
    class Version
    {
        private static string VersionString = "1.00";

        public static string Get() { return VersionString; }
    };

    class Program : CommandLineTool
    {
        static int Main(string[] args)
        {
            Program program = new Program();
            if (Debugger.IsAttached)
            {
                program.Run(args);
            }
            else
            {
                try
                {
                    program.Run(args);
                }
                catch (System.Exception e)
                {
                    Console.WriteLine("[ERROR] " + e.Message);
                    return 1;
                }
            }

            return 0;
        }

		static bool isCsvFile(string filename)
		{
			return filename.ToLowerInvariant().EndsWith(".csv");
		}
		static bool isCsvBinFile(string filename)
		{
			return filename.ToLowerInvariant().EndsWith(".csv.bin");
		}

		void Run(string[] args)
        {
			string formatString =
				"Format: \n" +
				"  <filename>\n" +
				"OR \n" +
				"  -in <filename>\n" +
				"  -outFormat=<csv|bin>\n" +
				"  [-o outFilename]\n" +
				"  [-force]\n"+
				"  [-verify]\n";


			// Read the command line
			if (args.Length < 1)
            {
                WriteLine(formatString);
                return;
            }


            ReadCommandLine(args);

			string inFilename;
			string outFormat;
			string outFilename = null;
			if (args.Length==1)
			{
				inFilename = args[0];
				// Determine the output format automatically
				if (isCsvBinFile(inFilename))
				{
					outFormat = "csv";
				}
				else if (isCsvFile(inFilename))
				{
					outFormat = "bin";
				}
				else
				{
					throw new Exception("Unknown input format!");
				}
			}
			else
			{
				// Advanced mode. Output format is required
				inFilename = GetArg("in",true);
				outFormat = GetArg("outformat", true);
				if (outFormat == "" || inFilename == "")
				{
					return;
				}
				outFilename = GetArg("o", null);

			}


			bool bBinOut=false;
			if (outFormat == "bin")
			{
				bBinOut = true;
			}
			else if (outFormat=="csv")
			{
				bBinOut = false;
			}
			else
			{
				throw new Exception("Unknown output format!");
			}

			string inFilenameWithoutExtension;
			if (isCsvBinFile(inFilename))
			{
				inFilenameWithoutExtension = inFilename.Substring(0, inFilename.Length - 8);
			}
			else if (isCsvFile(inFilename))
			{
				inFilenameWithoutExtension = inFilename.Substring(0, inFilename.Length - 4);
			}
			else
			{
				throw new Exception("Unexpected input filename extension!");
			}
			
			if (outFilename == null)
			{
				outFilename = inFilenameWithoutExtension + (bBinOut ? ".csv.bin" : ".csv");
			}

			if (outFilename.ToLowerInvariant() == inFilename.ToLowerInvariant())
			{
				throw new Exception("Input and output filenames can't match!");
			}

			if (System.IO.File.Exists(outFilename) && !GetBoolArg("force"))
			{
				throw new Exception("Output file already exists! Use -force to overwrite anyway.");
			}

			Console.WriteLine("Converting "+inFilename+" to "+outFormat+" format. Output filename: "+outFilename);
			Console.WriteLine("Reading input file...");
			CsvStats csvStats = CsvStats.ReadCSVFile(inFilename, null);

			Console.WriteLine("Writing output file...");
			if (bBinOut)
			{ 
				csvStats.WriteBinFile(outFilename);
			}
			else
			{
				csvStats.WriteToCSV(outFilename);
			}

			if (GetBoolArg("verify"))
			{
				// TODO: if verify is specified, use a temp intermediate file?
				// TODO: Check metadata, stat counts, stat names
				Console.WriteLine("Verifying output...");
				CsvStats csvStats2 = CsvStats.ReadCSVFile(outFilename, null);
				if (csvStats.SampleCount != csvStats2.SampleCount)
				{
					throw new Exception("Verify failed!");
				}
			}
		}
	}
}
