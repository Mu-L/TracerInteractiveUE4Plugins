// Copyright (C) Microsoft. All rights reserved.
// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Drawing;
using System.IO;
using System.Globalization;
using System.Diagnostics;
using System.Collections;

namespace CSVStats
{
	public class CsvEvent
    {
		public CsvEvent(CsvEvent source)
		{
			Name = source.Name;
			Frame = source.Frame;
		}
		public CsvEvent(string NameIn, int FrameIn)
        {
            Name = NameIn;
            Frame = FrameIn;
        }
        public CsvEvent()
        {

        }

        public string Name;
        public int Frame;
    };

    public class CsvMetadata
    {
        public Dictionary<string, string> Values;

        public CsvMetadata Clone()
        {
            CsvMetadata rv = new CsvMetadata();
            rv.Values = Values;
            return rv;
        }

		public CsvMetadata(CsvMetadata source)
		{
			Values = new Dictionary<string, string>();
			List<KeyValuePair<string, string>> pairList = source.Values.ToList();
			foreach (KeyValuePair<string, string> pair in pairList)
			{
				Values.Add(pair.Key, pair.Value);
			}
		}

		public CsvMetadata()
		{
			Values = new Dictionary<string, string>();
		}

		public CsvMetadata(string[] csvLine)
        {
            Values = new Dictionary<string, string>();
            TextInfo ti = CultureInfo.CurrentCulture.TextInfo;

            // Last line is key/value pairs. Commandline is the last entry, and needs special casing
            bool bIsKey = false;
            string key = "";
            string value = "";
            for ( int i=0; i<csvLine.Length; i++ )
            {
                string entry = csvLine[i];
                if ( entry.StartsWith("[") && entry.EndsWith("]") && !bIsKey)
                {
                    key = entry.Substring(1, entry.Length - 2).ToLowerInvariant();
                    bIsKey = true;
                }
                else
                {
                    if (key == "commandline")
                    {
                        value = "";
                        bool bFirst = true;
                        for (int j = i; j < csvLine.Length; j++)
                        {
                            bool bLast = ( j == csvLine.Length - 1 );
                            string segment = csvLine[j];
                            if (bFirst)
                            {
                                if (segment.StartsWith("\""))
                                {
                                    segment = segment.Substring(1);
                                }
                                bFirst = false;
                            }
                            if (bLast && segment.EndsWith("\""))
                            {
                                segment = segment.Substring(0,segment.Length-1);
                            }
                            value += segment;
                            if ( j<csvLine.Length-1 )
                            {
                                value += ",";
                            }
                        }
                        // We're done
                        i = csvLine.Length;
                    }
                    else
                    {
                        value = entry;
                    }

					if (Values.ContainsKey(key))
					{
						// If we're still writing to the current key, append the data onto the end
						Values[key] += "," + value;
					}
					else
					{
						Values.Add(key, value);
					}
					bIsKey = false;
                }

            }
        }

		public void WriteToBinaryFile(BinaryWriter fileWriter)
		{
			// Write metadata
			fileWriter.Write(Values.Count);
			foreach (string key in Values.Keys)
			{
				fileWriter.Write(key);
				fileWriter.Write(Values[key]);
			}
		}
		public void ReadFromBinaryFile(BinaryReader fileReader)
		{
			// Write metadata
			int numValues=fileReader.ReadInt32();
			for (int i=0;i<numValues;i++)
			{
				string key = fileReader.ReadString();
				string value = fileReader.ReadString();
				Values[key] = value;
			}
		}


		public void CombineAndValidate(CsvMetadata comparisonMetadata)
        {
            List<string> valuesDontMatchKeys = new List<string>();
            foreach (KeyValuePair<string, string> pair in Values)
            {
                bool bMatch = false;
                if ( comparisonMetadata.Values.ContainsKey(pair.Key))
                {
                    if ( comparisonMetadata.Values[pair.Key] == pair.Value )
                    {
                        bMatch = true; 
                    }
                }
                if ( !bMatch )
                {
                    valuesDontMatchKeys.Add(pair.Key);
                    break;
                }
            }

            foreach (string key in valuesDontMatchKeys)
            {
                Values[key] = "["+ key + " doesn't match!]";
            }
        }

        public static bool Matches(CsvMetadata a, CsvMetadata b)
        {
            if ( a == null && b == null )
            {
                return true;
            }
            if ( a == null )
            {
                return false;
            }
            if ( b == null )
            {
                return false;
            }
            if (a.Values.Count != b.Values.Count)
            {
                return false;
            }

            foreach (KeyValuePair<string, string> pair in a.Values)
            {
                bool bMatch = false;
               // if (pair.Key != "deviceprofile")
                {
                    if (b.Values.ContainsKey(pair.Key))
                    {
                        if (b.Values[pair.Key] == pair.Value)
                        {
                            bMatch = true;
                        }
                    }
                    if (!bMatch)
                    {
                        return false;
                    }
                }
            }
            return true;
        }


        public string GetValue(string key, string defaultValue)
        {
            if ( Values.ContainsKey(key))
            {
                return Values[key];
            }
            return defaultValue;
        }
    };



    public class StatSamples : IComparable
    {
        public StatSamples(StatSamples source, bool copySamples=true)
        {
            Name = source.Name;
            samples = new List<float>();
			if (copySamples)
			{
				samples.AddRange(source.samples);
			}
			average = source.average;
            total = source.total;
            colour = source.colour;
            LegendName = source.LegendName;
        }
        public StatSamples(string name, int sampleListLength=0)
        {
            Name = name;
            LegendName = name;
            if (sampleListLength == 0)
            {
                samples = new List<float>();
            }
            else
            {
                samples = new List<float>(new float[sampleListLength]);
            }
            average = 0.0f;
            total = 0.0f;
            colour = new Colour(0, 0.0f);
        }

        public float GetRatioOfFramesInBudget(float budget)
        {
            int countInBudget = 0;
            for (int i = 0; i < samples.Count; i++)
            {
                if (samples[i] <= budget)
                {
                    countInBudget++;
                }
            }
            return (float)(countInBudget) / (float)(samples.Count);
        }

        public int GetCountOfFramesOverBudget(float budget, bool IgnoreFirstFrame = true, bool IgnoreLastFrame = true)
        {
            int countOverBudget = 0;
            for (int i = IgnoreFirstFrame ? 1 : 0; i < samples.Count - (IgnoreLastFrame ? 1 : 0); i++)
            {
                if (samples[i] > budget)
                {
                    countOverBudget++;
                }
            }
            return countOverBudget;
        }


        public int GetCountOfFramesInRangeOverBudget(float budget, int startindex, int endindex)
        {
            int countOverBudget = 0;
            if (endindex > samples.Count)
            {
                endindex = samples.Count;
            }
            for (int i = startindex; i < endindex; i++)
            {
                if (samples[i] > budget)
                {
                    countOverBudget++;
                }
            }
            return countOverBudget;
        }



        public int CompareTo(object obj)
        {
            if (obj is StatSamples)
            {
                float objAverage = (obj as StatSamples).average;
                if (average > objAverage) return -1;
                if (average < objAverage) return 1;
                return 0;
                //                return (int)((objAverage - average) * 100000000.0f);
            }
            throw new ArgumentException("Object is not a StatSamples");
        }


        public float ComputeAverage(int minSample=0, int maxSample=-1)
        {
            if (maxSample == -1)
            {
                maxSample = samples.Count;
            }
			double localTotal = 0.0;
            for (int i = minSample; i < maxSample; i++)
            {
				localTotal += (double)samples[i];
            }
			return (float)(localTotal / (double)(maxSample - minSample));
		}

		public void ComputeAverageAndTotal(int minSample = 0, int maxSample = -1)
        {
            if (maxSample == -1)
            {
                maxSample = samples.Count;
            }
            total = 0.0;
            for (int i = minSample; i < maxSample; i++)
            {
                total += (double)samples[i];
            }
            average = (float)(total / (double)(maxSample - minSample));
        }

        public float ComputeMaxValue(int minSample = 0, int maxSample = -1)
        {
            if (maxSample == -1)
            {
                maxSample = samples.Count;
            }
            float maxValue = -float.MaxValue;
            for (int i = minSample; i < maxSample; i++)
            {
                maxValue = Math.Max(maxValue, samples[i]);
            }
            return maxValue;
        }
        public float ComputeMinValue(int minSample = 0, int maxSample = -1)
        {
            if (maxSample == -1)
            {
                maxSample = samples.Count;
            }
            float minValue = float.MaxValue;
            for (int i = minSample; i < maxSample; i++)
            {
                minValue = Math.Min(minValue, samples[i]);
            }
            return minValue;
        }

        public int GetNumSamples() { return samples.Count; }
        public string Name;
        public string LegendName;
        public List<float> samples;
        public float average;
        public double total;
        public Colour colour;
    };


	public class CsvStats
	{
		private static int CsvBinVersion = 2;
		public CsvStats()
		{
			Stats = new Dictionary<string, StatSamples>();
			Events = new List<CsvEvent>();
		}
		public CsvStats(CsvStats sourceCsvStats, string[] statNamesToFilter = null)
		{
			Stats = new Dictionary<string, StatSamples>();
			Events = new List<CsvEvent>();
			if (statNamesToFilter != null)
			{
				foreach (string statName in statNamesToFilter)
				{
					List<StatSamples> stats = sourceCsvStats.GetStatsMatchingString(statName);
					foreach (StatSamples sourceStat in stats)
					{
						string key = sourceStat.Name.ToLower();
						if (!Stats.ContainsKey(key))
						{
							Stats.Add(key, new StatSamples(sourceStat));
						}
					}
				}
			}
			else
			{
				foreach (StatSamples sourceStat in sourceCsvStats.Stats.Values)
				{
					AddStat(new StatSamples(sourceStat));
				}
			}
			foreach (CsvEvent ev in sourceCsvStats.Events)
			{
				Events.Add(new CsvEvent(ev));
			}
			if (sourceCsvStats.metaData != null)
			{
				metaData = new CsvMetadata(sourceCsvStats.metaData);
			}
		}
		public StatSamples GetStat(string name)
		{
			name = name.ToLower();
			if (Stats.ContainsKey(name))
			{
				return Stats[name];
			}
			return null;
		}
		public void AddStat(StatSamples stat)
		{
			Stats.Add(stat.Name.ToLower(), stat);
		}

		public void CropStats(int minFrame, int maxFrame = Int32.MaxValue)
		{
			if (maxFrame <= minFrame)
			{
				return;
			}
			if (minFrame == 0 && maxFrame >= SampleCount)
			{
				return;
			}

			// Remove stats outside the range
			foreach (StatSamples stat in Stats.Values.ToArray())
			{
				int start = Math.Max(minFrame, 0);
				int end = Math.Max(Math.Min(maxFrame, stat.samples.Count), 0);
				if (maxFrame == Int32.MaxValue)
				{
					end = stat.samples.Count;
				}
				int count = end - start;
				List<float> NewSamples = new List<float>(count);
				for (int i = 0; i < count; i++)
				{
					NewSamples.Add(stat.samples[i + start]);
				}
				stat.samples = NewSamples;
			}

			// Filter the filtered events
			List<CsvEvent> newEvents = new List<CsvEvent>();
			foreach (CsvEvent ev in Events)
			{
				if (ev.Frame >= minFrame && ev.Frame <= maxFrame)
				{
					ev.Frame -= minFrame; // Offset the frame based on the new start frame
					newEvents.Add(ev);
				}
			}

			ComputeAveragesAndTotal();

			Events = newEvents;
		}

		static string[] ReadLinesFromFile(string filename)
		{
			StreamReader reader = new StreamReader(filename, true);
			List<string> lines = new List<string>();

			// Detect unicode
			string line = reader.ReadLine();

			bool bIsUnicode = false;
			for (int i = 0; i < line.Length - 1; i++)
			{
				if (line[i] == '\0')
				{
					bIsUnicode = true;
					break;
				}
			}
			if (bIsUnicode)
			{
				reader = new StreamReader(filename, Encoding.Unicode, true);
			}
			else
			{
				lines.Add(line);
			}

			while ((line = reader.ReadLine()) != null)
			{
				// Strip off empty lines
				if (line.Trim().Length > 0)
				{
					lines.Add(line);
				}
			}

			return lines.ToArray();
		}

		public static bool DoesSearchStringMatch(string str, string searchString)
		{
			searchString = searchString.Trim().ToLower();
			if (searchString.EndsWith("*"))
			{
				searchString = searchString.Substring(0, searchString.Length - 1);
				return str.ToLower().StartsWith(searchString);
			}
			else
			{
				return searchString == str.ToLower();
			}
		}


		public Dictionary<string, bool> GetStatNamesMatchingStringList_Dict(string[] statNames)
		{
			Dictionary<string, bool> uniqueStatsDict = new Dictionary<string, bool>();
			foreach (string statStr in statNames)
			{
				List<StatSamples> statsMatching = GetStatsMatchingString(statStr);
				foreach (StatSamples stat in statsMatching)
				{
					if (!uniqueStatsDict.ContainsKey(stat.Name))
					{
						uniqueStatsDict.Add(stat.Name, true);
					}
				}
			}
			return uniqueStatsDict;
		}

		public List<string> GetStatNamesMatchingStringList(string[] statNames)
		{
			return GetStatNamesMatchingStringList_Dict(statNames).Keys.ToList();
		}

		public List<StatSamples> GetStatsMatchingString(string statString)
		{
			bool isWild = false;
			statString = statString.Trim().ToLower();
			if (statString.EndsWith("*"))
			{
				isWild = true;
				statString = statString.TrimEnd('*');
			}
			List<StatSamples> statList = new List<StatSamples>();
			foreach (StatSamples stat in Stats.Values)
			{
				if (isWild && stat.Name.ToLower().StartsWith(statString))
				{
					statList.Add(stat);
				}
				else if (stat.Name.ToLower() == statString.ToLower())
				{
					statList.Add(stat);
				}
			}
			return statList;
		}

		public void WriteBinFile(string filename)
		{
			System.IO.FileStream fileStream = new FileStream(filename, FileMode.Create);
			System.IO.BinaryWriter fileWriter = new System.IO.BinaryWriter(fileStream);

			// Write the header
			fileWriter.Write("CSVBIN");
			fileWriter.Write(CsvBinVersion);
			bool hasMetadata = (metaData != null);
			fileWriter.Write(hasMetadata);
			if (hasMetadata)
			{
				metaData.WriteToBinaryFile(fileWriter);
			}
			fileWriter.Write(Events.Count);
			fileWriter.Write(SampleCount);
			fileWriter.Write(Stats.Count);
			foreach (StatSamples stat in Stats.Values)
			{
				fileWriter.Write(stat.Name);
			}

			// Write the stats
			foreach (StatSamples stat in Stats.Values)
			{
				fileWriter.Write(stat.Name);
				if (stat.samples.Count != SampleCount)
				{
					throw new Exception("Sample count doesn't match!");
				}
				fileWriter.Write(stat.average);
				fileWriter.Write(stat.total);
				List<float> uniqueValues = new List<float>();
				FileEfficientBitArray statUniqueMask = new FileEfficientBitArray(SampleCount); 
				float oldVal = float.NegativeInfinity;
				for (int i = 0; i < stat.samples.Count; i++)
				{
					float val = stat.samples[i];
					if (val != oldVal || i==0)
					{
						uniqueValues.Add(val);
						statUniqueMask.Set(i);
					}
					oldVal = val;
				}
				int statDataSizeBytes = statUniqueMask.GetSizeBytes() + uniqueValues.Count * sizeof(float) + sizeof(int);
				fileWriter.Write(statDataSizeBytes);
				
				long statStartOffset = fileWriter.BaseStream.Position;
				statUniqueMask.WriteToFile(fileWriter);
				fileWriter.Write(uniqueValues.Count);
				foreach (float val in uniqueValues)
				{
					fileWriter.Write(val);
				}

				// Check the stat data size matches what we wrote
				int measuredStatDataSize = (int)(fileWriter.BaseStream.Position-statStartOffset);
				if (statDataSizeBytes != measuredStatDataSize)
				{
					throw new Exception("Stat data size is wrong!");
				}

			}
			// Write the events
			foreach (CsvEvent ev in Events)
			{
				fileWriter.Write(ev.Frame);
				fileWriter.Write(ev.Name);
			}
			fileWriter.Close();
		}

		public static CsvStats ReadBinFile(string filename, string[] statNamesToRead=null, int numRowsToSkip=0, bool justHeader=false)
		{
			System.IO.FileStream fileStream = new FileStream(filename, FileMode.Open);
			System.IO.BinaryReader fileReader = new System.IO.BinaryReader(fileStream);

			// Read the header
			if ( fileReader.ReadString() != "CSVBIN" )
			{
				throw new Exception("Failed to read "+filename+". Bad format");
			}
			int version=fileReader.ReadInt32();
			if (version != CsvBinVersion)
			{
				throw new Exception("Failed to read "+filename+". Version mismatch. Version is "+version.ToString()+". Expected: "+CsvBinVersion.ToString());
			}
			CsvStats csvStatsOut = new CsvStats();
			bool hasMetadata = fileReader.ReadBoolean();
			if (hasMetadata)
			{
				csvStatsOut.metaData = new CsvMetadata();
				csvStatsOut.metaData.ReadFromBinaryFile(fileReader);
			}
			int eventCount = fileReader.ReadInt32();
			int sampleCount = fileReader.ReadInt32();
			int statCount = fileReader.ReadInt32();

			List<string> statNames = new List<string>();
			for (int s = 0; s < statCount; s++)
			{
				string statName = fileReader.ReadString();
				StatSamples stat = new StatSamples(statName);
				csvStatsOut.AddStat(stat);
				statNames.Add(statName);
			}

			// Get the list of all stats
			Dictionary<string, bool> statNamesToReadDict = null;
			if (statNamesToRead != null)
			{
				statNamesToReadDict = csvStatsOut.GetStatNamesMatchingStringList_Dict(statNamesToRead);
			}

			if (justHeader)
			{
				fileReader.Close();
				return csvStatsOut;
			}

			// Read the stats
			foreach (string headerStatName in statNames)
			{
				string statName = fileReader.ReadString();
				if (statName != headerStatName)
				{
					throw new Exception("Failed to read " + filename + ". Stat name doesn't match header!");
				}
				float statAverage = fileReader.ReadSingle();
				double statTotal = fileReader.ReadDouble();
				int statSizeBytes = fileReader.ReadInt32();
				bool readThisStat = statNamesToReadDict==null || statNamesToReadDict.ContainsKey(statName);
				if (readThisStat==false)
				{
					// If we're skipping this stat then just read the bytes and remove it from the stat list
					byte[] bytes = fileReader.ReadBytes(statSizeBytes);
					if (!csvStatsOut.Stats.Remove(statName.ToLowerInvariant()) )
					{
						throw new Exception("Unexpected error. Stat " + statName + " wasn't found!");
					}
					continue;
				}
				StatSamples statOut = csvStatsOut.GetStat(statName);

				statOut.total = statTotal;
				statOut.average = statAverage;

				// Read the mask
				FileEfficientBitArray statUniqueMask = new FileEfficientBitArray();
				statUniqueMask.ReadFromFile(fileReader);

				int uniqueValueCount = fileReader.ReadInt32();
				float[] uniqueValues = new float[uniqueValueCount];
				byte[] uniqueValuesBuffer=fileReader.ReadBytes(uniqueValueCount * 4);
				Buffer.BlockCopy(uniqueValuesBuffer, 0, uniqueValues, 0, uniqueValuesBuffer.Length);

				// Decode the samples from the mask and unique list
				if (statUniqueMask.Count != sampleCount)
				{
					throw new Exception("Failed to read " + filename + ". Stat sample count doesn't match header!");
				}
				float currentVal = 0;
				int uniqueListIndex = 0;
				statOut.samples.Capacity = statUniqueMask.Count;
				//statOut.samples.AddRange(Enumerable.Repeat(0.0f, statUniqueMask.Count));
				for (int i=0; i<statUniqueMask.Count; i++)
				{
					if (statUniqueMask.Get(i))
					{
						currentVal = uniqueValues[uniqueListIndex];
						uniqueListIndex++;
					}
					statOut.samples.Add(currentVal);
					//statOut.samples[i]=currentVal;
				}
				if (numRowsToSkip > 0)
				{
					statOut.samples.RemoveRange(0, numRowsToSkip);
				}
			}
			// Read the events
			for (int i = 0; i < eventCount; i++)
			{
				int frame = fileReader.ReadInt32();
				string name = fileReader.ReadString();
				if (frame >= numRowsToSkip)
				{
					CsvEvent ev = new CsvEvent(name, frame);
					csvStatsOut.Events.Add(ev);
				}
			}
			fileReader.Close();
			return csvStatsOut;
		}

		public void WriteToCSV(string filename)
        {
            System.IO.StreamWriter csvOutFile;
            csvOutFile = new System.IO.StreamWriter(filename);

            // Store the events in a dictionary so we can lookup by frame
            Dictionary<int, string> eventsLookup = new Dictionary<int, string>();
            foreach (CsvEvent ev in Events)
            {
                if (!eventsLookup.ContainsKey(ev.Frame))
                {
                    eventsLookup.Add(ev.Frame, ev.Name);
                }
                else
                {
                    eventsLookup[ev.Frame] += ";" + ev.Name;
                }
            }

            // ensure there's an events stat
            if (Events.Count > 0)
            {
                if (!Stats.ContainsKey("events"))
                {
                    Stats.Add("events", new StatSamples("events"));
                }
            }

            // Write the headings
            int sampleCount = Int32.MaxValue;
            bool first = true;
            StringBuilder sb = new StringBuilder();
            int statIndex = 0;
            foreach (StatSamples srcStat in Stats.Values.ToArray())
            {
                if (first)
                {
                    sampleCount = srcStat.samples.Count;
                }

                if (sampleCount > 0 || srcStat.Name.ToLower() == "events")
                {
                    if (!first)
                    {
                        sb.Append(',');
                    }
                    sb.Append(srcStat.Name);
                    first = false;
                }
                statIndex++;
            }
            csvOutFile.WriteLine(sb);
            sb.Clear();

            // Write the stats
            if (sampleCount == Int32.MaxValue)
            {
                sampleCount = 0;
            }
            for (int i = 0; i < sampleCount; i++)
            {
                first = true;
                foreach (StatSamples srcStat in Stats.Values.ToArray())
                {
                    if (!first)
                    {
                        sb.Append(',');
                    }
                    if (srcStat.samples.Count == 0 && srcStat.Name.ToLower() == "events")
                    {
                        if (eventsLookup.ContainsKey(i))
                        {
                            sb.Append(eventsLookup[i]);
                        }
                    }
                    else
                    {
                        if (i < srcStat.samples.Count)
                        {
                            sb.Append(srcStat.samples[i]);
                        }
                        else
                        {
                            sb.Append("0");
                        }
                    }
                    first = false;
                }
                csvOutFile.WriteLine(sb);
                sb.Clear();
            }

            if (metaData != null)
            {
                int index = 0;
                foreach (System.Collections.Generic.KeyValuePair<string, string> pair in metaData.Values)
                {
                    if (index > 0)
                    {
                        sb.Append(",");
                    }
					string value = pair.Value;

					// Override header row at end metadata, because it's not
					if (pair.Key.ToLower() == "hasheaderrowatend")
					{
						value = "0";
					}
					if (pair.Key.ToLower() == "commandline")
					{
						value = "\"" + value + "\"";
					}
					sb.Append("["+pair.Key + "]," + value);
                    index++;
                }
            }
            csvOutFile.WriteLine(sb);
            csvOutFile.Close();
            // Write the metadata
        }

		// Pad the stats
		public void ComputeAveragesAndTotal()
        {
            foreach (StatSamples stat in Stats.Values.ToArray())
            {
                stat.ComputeAverageAndTotal();
            }
        }

        public void Combine(CsvStats srcStats, bool sumRows = false, bool computeAverage = true)
        {
            if (metaData != null)
            {
                metaData.CombineAndValidate(srcStats.metaData);
            }

            // Collate the stats, removing ones which don't exist in both
            int EventFrameOffset = 0;
            Dictionary<string, StatSamples> newStats = new Dictionary<string, StatSamples>();
            foreach (StatSamples srcStat in srcStats.Stats.Values.ToArray())
            {
                if (Stats.ContainsKey(srcStat.Name.ToLower()))
                {
                    StatSamples destStat = GetStat(srcStat.Name);
					if (sumRows)
					{
						int n = Math.Min(srcStat.samples.Count, destStat.samples.Count);
						// Combine initial stats
						for (int i=0;i<n; i++)
						{
							destStat.samples[i] += srcStat.samples[i];
						}
						// Add the remaining stats
						for (int i=n; i< srcStat.samples.Count; i++)
						{
							destStat.samples.Add(srcStat.samples[i]);
						}
					}
					else
					{
						EventFrameOffset = Math.Max(EventFrameOffset, destStat.samples.Count);
						foreach (float sample in srcStat.samples)
						{
							destStat.samples.Add(sample);
						};
					}
                    newStats.Add(srcStat.Name.ToLower(), destStat);
                }
            }

            if (computeAverage)
            {
                // Compute the new average
                foreach (StatSamples stat in newStats.Values.ToArray())
                {
                    stat.ComputeAverageAndTotal();
                }
            }

			if ( sumRows)
			{
				// just remove events. Averaging is complicated/doesn't really make sense
				Events.Clear();
			}
			else
			{
				// Add the events, offsetting the frame numbers
				foreach (CsvEvent ev in srcStats.Events)
				{
					ev.Frame += EventFrameOffset;
					Events.Add(ev);
				}
			}
			
            Stats = newStats;
        }


		// This will average only those stats that exist in all the stat sets
		// Stats are averaged per frame, and the the final length is be equal to the longest CSV
		// If CSVs are of varying length, this means later frames will be averaged over fewer samples than earlier frames
		public static CsvStats AverageByFrame(CsvStats[] statsToAvg, bool bStatsAvarage = false)
        {
            CsvStats avgStats = new CsvStats();
            if (statsToAvg.Length > 0)
            {
                // We need to have all the frame per each stat in the file
                int maxFrames = 0;

                // Use the first as stat name basis
                string[] statKeys = statsToAvg[0].Stats.Keys.ToArray();
                foreach (string statName in statKeys)
                {
                    int maxSamples = 0;
                    int statCount = 0;
                    foreach (CsvStats stats in statsToAvg)
                    {
                        // Remove from the set of new stats if
                        // it doesn't exist in one of the set.
                        if (stats.Stats.ContainsKey(statName))
                        {
                            maxSamples = Math.Max(stats.Stats[statName].samples.Count, maxSamples);
                            statCount++;
                        }
                    }

                    if (statCount == statsToAvg.Length && maxSamples > 0)
                    {
                        avgStats.AddStat(new StatSamples(statName));
                        maxFrames = Math.Max(maxFrames, maxSamples);
                    }
                }

                // Copy meta data
                avgStats.metaData = statsToAvg[0].metaData;
                if (avgStats.metaData != null)
                {
                    foreach (CsvStats stats in statsToAvg)
                    {
                        avgStats.metaData.CombineAndValidate(stats.metaData);
                    }
                }

                foreach (string statName in avgStats.Stats.Keys)
                {
                    // This should always exist
                    StatSamples avgSamples = avgStats.GetStat(statName);
                    if (avgSamples != null)
                    {
						List<int> sampleCounts = new List<int>();
						sampleCounts.AddRange(Enumerable.Repeat(0, maxFrames));

						// Initialise sample to 0.0
						avgSamples.samples.AddRange(Enumerable.Repeat(0.0f, maxFrames));

                        // Add samples from other stats
                        foreach (CsvStats stats in statsToAvg)
                        {
                            StatSamples statSamples = stats.GetStat(statName);
                            if ((statSamples != null) && (avgSamples.samples.Count >= statSamples.samples.Count))
                            {
								// This should always be true: avgSamples.samples.Count >= statSamples.samples.Count
								for (int i = 0; i < statSamples.samples.Count; i++)
								{
									avgSamples.samples[i] += statSamples.samples[i];
									sampleCounts[i] += 1;
                                }
                            }
                        }

                        // Average the samples
                        for (int i = 0; i < avgSamples.samples.Count; i++)
                        {
                            avgSamples.samples[i] /= (float)sampleCounts[i];
                        }
                    }

                    if (bStatsAvarage)
                    {
                        avgSamples.ComputeAverageAndTotal();
                    }
                }
            }

            return avgStats;
        }


        public static CsvMetadata ReadCSVMetadata(string csvFilename)
        {
            string lastLine = File.ReadLines(csvFilename).Last();
            
            if (LineIsMetadata(lastLine))
            {
                string[] values = lastLine.Split(',');
                return new CsvMetadata(values);
            }
            return null;
        }
        public static CsvMetadata ReadCSVMetadataFromLines(string [] csvFileLines)
        {
            string lastLine = csvFileLines.Last();
            if (LineIsMetadata(lastLine))
            {
                string[] values = lastLine.Split(',');
                return new CsvMetadata(values);
            }
            return null;
        }

        public void GetEventFrameIndexDelimiters(string startString, string endString, out List<int> startIndices, out List<int> endIndices)
        {
            bool startIsWild = false;
            bool endIsWild = false;
            startString = startString.ToLower().Trim();
            if (endString != null)
            {
                endString = endString.ToLower().Trim();
                if (endString.EndsWith("*"))
                {
                    endIsWild = true;
                    endString = endString.TrimEnd('*');
                }
            }
            if (startString.EndsWith("*"))
            {
                startIsWild = true;
                startString = startString.TrimEnd('*');
            }

            bool insidePair = false;
            startIndices = new List<int>();
            endIndices = new List<int>();
            for (int i = 0; i < Events.Count; i++)
            {
                CsvEvent csvEvent = Events[i];
                string evName = csvEvent.Name.ToLower();
                string strToMatch = insidePair ? endString : startString;
                bool isWild = insidePair ? endIsWild : startIsWild;

                bool found = false;
                if (strToMatch != null)
                {
                    if (isWild)
                    {
                        if (evName.StartsWith(strToMatch))
                        {
                            found = true;
                        }
                    }
                    else if (evName == strToMatch)
                    {
                        found = true;
                    }
                }

                if (found)
                {
                    if (insidePair)
                    {
                        endIndices.Add(csvEvent.Frame);
                    }
                    else
                    {
                        startIndices.Add(csvEvent.Frame);
                    }
                    insidePair = !insidePair;
                }
            }

            // If the end event was missing, add it at the end
            if ( endIndices.Count == startIndices.Count - 1)
            {
                endIndices.Add(SampleCount);
            }
        }

        public CsvStats StripByEvents(string startString, string endString, bool invert, out int numFramesStripped)
        {
			CsvStats newCsvStats = new CsvStats();
            List<int> startIndices = null;
            List<int> endIndices = null;
            GetEventFrameIndexDelimiters(startString, endString, out startIndices, out endIndices);
			numFramesStripped = 0;
			if (startIndices.Count == 0 )
            {
                return this;
            }

			numFramesStripped = -1;
			int frameCount = 0;
            // Strip out samples and recompute averages
            foreach (StatSamples stat in Stats.Values)
            {
				StatSamples newStat = new StatSamples(stat, false);
				newStat.samples = new List<float>(SampleCount);
                int stripEventIndex = 0;
                for (int i = 0; i < stat.samples.Count; i++)
                {
                    int startIndex = (stripEventIndex < startIndices.Count) ? startIndices[stripEventIndex] : stat.samples.Count;
                    int endIndex = (stripEventIndex < endIndices.Count) ? endIndices[stripEventIndex] : stat.samples.Count;
                    if (i < startIndex)
                    {
						newStat.samples.Add(stat.samples[i]);
                    }
                    else
                    {
                        if ( i == endIndex )
                        {
                            stripEventIndex++;
                        }
                    }
                }
                if (numFramesStripped == -1)
                {
					numFramesStripped = stat.samples.Count - newStat.samples.Count;
                    frameCount = stat.samples.Count;
                }

                newStat.ComputeAverageAndTotal();
				newCsvStats.AddStat(newStat);
            }

            // Strip out the events
            int FrameOffset = 0;
            {
                int stripEventIndex = 0;
                for (int i = 0; i < Events.Count; i++)
                {
                    CsvEvent csvEvent = Events[i];
                    int startIndex = (stripEventIndex < startIndices.Count) ? startIndices[stripEventIndex] : frameCount;
                    int endIndex = (stripEventIndex < endIndices.Count) ? endIndices[stripEventIndex] : frameCount;
                    CsvEvent newEvent = new CsvEvent(csvEvent.Name, csvEvent.Frame + FrameOffset);
                    if (csvEvent.Frame < startIndex)
                    {
						newCsvStats.Events.Add(newEvent);
                    }
                    else
                    {
                        if (csvEvent.Frame == startIndex)
                        {
							// Check if this is the last event this frame
							if (i == Events.Count - 1 || Events[i + 1].Frame != csvEvent.Frame)
							{
								// Subsequent events will get offset by this amount
								FrameOffset -= endIndex - startIndex;
							}
                        }
                        if (csvEvent.Frame == endIndex+1)
                        {
							newCsvStats.Events.Add(newEvent);
                            stripEventIndex++;
                        }
                    }
                }
            }
			newCsvStats.metaData = metaData;
			return newCsvStats;
		}

        public int SampleCount
        {
            get
            {
                if ( Stats.Count == 0 )
                {
                    return 0;
                }
                else
                {
                    return Stats.First().Value.samples.Count;
                }
            }
        }

		public static bool DoesMetadataMatchFilter(CsvMetadata metadata, string metadataFilterString)
		{
			string[] keyValuePairStrs = metadataFilterString.Split(',');
			foreach (string keyValuePairStr in keyValuePairStrs)
			{
				string[] keyValue = keyValuePairStr.Split('=');
				if (keyValue.Length != 2)
				{
					return false;
				}
				string key = keyValue[0].ToLower();
				if (!metadata.Values.ContainsKey(key))
				{
					return false;
				}
				if (metadata.Values[key].ToLower() != keyValue[1].ToLower())
				{
					return false;
				}
			}
			return true;
		}


		public static CsvStats ReadCSVFile(string csvFilename, string[] statNames, int numRowsToSkip = 0)
        {
			if (csvFilename.EndsWith(".csv.bin"))
			{
				return ReadBinFile(csvFilename, statNames, numRowsToSkip);
			}
			else
			{
				string[] lines = ReadLinesFromFile(csvFilename);
				return ReadCSVFromLines(lines, statNames, numRowsToSkip);
			}
        }


		static bool LineIsMetadata(string line)
        {
            if (line.Trim().StartsWith("["))
            {
                return true;
            }
            return false;
        }

        public static CsvStats ReadCSVFromLines(string[] linesArray, string[] statNames, int numRowsToSkip = 0, bool skipReadingData=false)
        {
            List<string> lines = linesArray.ToList();

            // Check if we have any metadata
            bool bHasMetaData = LineIsMetadata(lines.Last());

			CsvMetadata metaData = null;

			// Add the metadata to the stats collection
			if (bHasMetaData)
			{
				string[] lastLine = lines.Last().Split(',');
				metaData = new CsvMetadata(lastLine);

				// New CSVs from the csv profiler have a header row at the end of the file,
				// since the profiler writes out the file incrementally.
				if ("1" == metaData.GetValue("hasheaderrowatend", null))
				{
					// Swap the header row for the one at the end of the file,
					// then remove the end one.
					lines[0] = lines[lines.Count - 2];
					lines.RemoveAt(lines.Count - 2);
				}
			}

			if ( numRowsToSkip > 0)
            {
                lines.RemoveRange(1, numRowsToSkip);
            }
			string[] headings = lines[0].Split(',');

            if (lines.Count > 2 && lines[lines.Count - 1] == "\"" )
            {
                lines[lines.Count - 2] += lines[lines.Count - 1];
                lines.RemoveAt(lines.Count - 1);
            }

            if (skipReadingData)
            {
				int dataLineCount = bHasMetaData ? lines.Count-2 : lines.Count-1;
                lines.RemoveRange(1, dataLineCount);
            }

			// Get the list of lower case stat names, expanding wildcards
			string[] statNamesLowercase = null;
            if (statNames != null)
            {
                statNamesLowercase = statNames.Select(s => s.ToLowerInvariant()).ToArray();
                {
                    // Expand the list of stat names based on the wildcards and the headers. We do this here to make sorting simpler
                    HashSet<string> newStatNamesLowercase = new HashSet<string>();
                    foreach (string statname in statNamesLowercase)
                    {
                        if (statname.EndsWith("*"))
                        {
                            int index = statname.LastIndexOf('*');
                            string prefix = statname.Substring(0, index);
                            // Expand all the stat names
                            foreach (string headingStat in headings)
                            {
                                if (headingStat.ToLower().StartsWith(prefix))
                                {
                                    newStatNamesLowercase.Add(headingStat.ToLower());
                                }
                            }
                        }
                        else
                        {
                            newStatNamesLowercase.Add(statname);
                        }
                    }

                    statNamesLowercase = newStatNamesLowercase.ToArray();
                }
            }

            // First line is headings, last line contains build info 
            int numSamples = lines.Count - (bHasMetaData ? 2 : 1);

            // Create the stats
            int eventHeadingIndex = -1;
            StatSamples[] stats = new StatSamples[headings.Length];
            for (int i = 0; i < headings.Length; i++)
            {
                string heading = headings[i].Trim();
				if ( heading == "")
				{
					continue;
				}
                // find the events column (if there is one)
                else if (heading.ToLower() == "events")
                {
                    eventHeadingIndex = i;
                }
                else if (statNamesLowercase == null || statNamesLowercase.Contains(heading.ToLower()))
                {
                    stats[i] = new StatSamples(heading, numSamples);
                }
            }


            List<CsvEvent> FilteredEvents = new List<CsvEvent>();

            if (!skipReadingData)
            {
                string[] eventStrings = new string[numSamples];

//                for (int i = 1; i < numSamples + 1; i++)
                Parallel.For(1, numSamples + 1,
                                     i =>
                {
                    int sampleIndex = i - 1;
                    int statIndex = 0;
                    string line = lines[i] + "\n";
                    for (int j = 0; j < line.Length; j++)
                    {
                        // Note: we check statIndex<stats.length here in case of truncated CSVs
                        if (statIndex < stats.Length && stats[statIndex] != null)
                        {
                            // Read the stat
                            float value = 0.0f;

                            // Skip whitespace
                            while (line[j] == ' ')
                            {
                                j++;
                            }

                            bool negative = false;
                            if ( line[j] == '-' )
                            {
                                negative = true;
                                j++;
                            }

                            // Read the nonfractional part of the number
                            int num = 0;
                            while (line[j] >= '0' && line[j] <= '9')
                            {
                                num *= 10;
                                num += line[j] - '0';
                                j++;
                            }
                            value = (float)num;

                            if (line[j] == '.')
                            {
                                // read fractional part
                                num = 0;
                                j++;
                                float multiplier = 0.1f;
                                while (line[j] >= '0' && line[j] <= '9')
                                {
                                    value += (float)(line[j] - '0') * multiplier;
                                    j++;
                                    multiplier *= 0.1f;
                                }
                            }

                            if ( negative )
                            {
                                value = -value;
                            }

                            stats[statIndex].samples[sampleIndex] = value;

                            // Skip everything else until the next newline or comma
                            while (line[j] != ',' && line[j] != '\n')
                            {
                                j++;
                            }
                        }
                        else
                        {
                            // Skip parsing
                            int startJ = j;
                            while (line[j] != ',' && line[j] != '\n')
                            {
                                j++;
                            }
                            if (statIndex == eventHeadingIndex)
                            {
                                eventStrings[sampleIndex] = line.Substring(startJ, j - startJ);
                            }
                        }
                        statIndex++;
                    }
                }
                ); // Needed by parallel for

                // Read events
                for (int i = 0; i < eventStrings.Length; i++)
                {
                    string eventString = eventStrings[i];
                    if (!string.IsNullOrEmpty(eventString))
                    {
                        string[] Events = eventString.Split(';');
                        foreach (string EventString in Events)
                        {
                            if (EventString.Length > 0)
                            {
                                CsvEvent ev = new CsvEvent();
                                ev.Frame = i;
                                ev.Name = EventString;
                                FilteredEvents.Add(ev);
                            }
                        }
                    }
                }
            }

            // Make sure the stat ordering matches the order they're passed in
            CsvStats csvStats = new CsvStats();

            if (statNamesLowercase != null)
            {
                CsvStats unorderedCsvStats = new CsvStats();
                foreach (StatSamples statSamples in stats)
                {
                    if (statSamples != null)
                    {
                        // Combine stats if we find a duplicate
                        if (unorderedCsvStats.Stats.ContainsKey(statSamples.Name.ToLower()))
                        {
                            StatSamples existingStat = unorderedCsvStats.GetStat(statSamples.Name);
                            for (int i = 0; i < statSamples.samples.Count; i++)
                            {
                                existingStat.samples[i] += statSamples.samples[i];
                            }
                        }
                        else
                        {
                            unorderedCsvStats.AddStat(statSamples);
                        }
                    }
                }

                foreach (string statName in statNamesLowercase)
                {
                    StatSamples stat = unorderedCsvStats.GetStat(statName);
                    if (stat != null)
                    {
                        csvStats.AddStat(stat);
                    }
                }
            }
            else
            {
                int c = 0;
                foreach (StatSamples statSamples in stats)
                {
                    c++;
                    if (statSamples != null)
                    {
                        if (csvStats.Stats.ContainsKey(statSamples.Name.ToLower()))
                        {
                            // Combine stats if we find a duplicate                
                            StatSamples existingStat = csvStats.GetStat(statSamples.Name);
                            for (int i = 0; i < statSamples.samples.Count; i++)
                            {
                                existingStat.samples[i] += statSamples.samples[i];
                            }
                        }
                        else
                        {
                            csvStats.AddStat(statSamples);
                        }
                    }
                }
            }

            // Compute averages
            foreach (StatSamples stat in csvStats.Stats.Values.ToArray())
            {
                stat.ComputeAverageAndTotal();
            }

			csvStats.metaData = metaData;

            csvStats.Events = FilteredEvents;
            return csvStats;
        }


        public Dictionary<string, StatSamples> Stats;
        public List<CsvEvent> Events;
        public CsvMetadata metaData;
    };


    public class Colour
    {
		public Colour(byte rIn, byte gIn, byte bIn, float alphaIn = 1.0f) { r = rIn; g = gIn; b = bIn; alpha = alphaIn; }
        public Colour(uint hex, float alphaIn = 1.0f)
        {
            r = (byte)((hex >> 16) & 0xff);
            g = (byte)((hex >> 8) & 0xff);
            b = (byte)((hex >> 0) & 0xff);
            alpha = alphaIn;
        }
        public Colour(Colour colourIn) { r = colourIn.r; g = colourIn.g; b = colourIn.b; alpha = colourIn.alpha; }

        public string SVGString()
        {
            return "'" + SVGStringNoQuotes() + "'";
        }
        public string SVGStringNoQuotes()
        {
            if (alpha >= 1.0f)
            {
                return "rgb(" + r + ", " + g + ", " + b + ")";
            }
            else
            {
                return "rgba(" + r + ", " + g + ", " + b + ", " + alpha + ")";
            }
        }

        public static Colour White = new Colour(255, 255, 255);
        public static Colour Black = new Colour(0, 0, 0);

        public byte r, g, b;
        public float alpha;
    };

	// BitArray doesn't allow us to access actual words, which is horribly slow when you need to read/write one bit at a time
	class FileEfficientBitArray
	{
		public FileEfficientBitArray()
		{
			bitCount = 0;
		}
		public FileEfficientBitArray(int inBitCount)
		{
			bitCount = inBitCount;
			int wordCount = ((bitCount + 31) / 32);
			words = new uint[wordCount];
		}

		public bool Get(int bitIndex)
		{
			int wordIndex = bitIndex >> 5;
			int b = bitIndex & 0x0000001F;
			return (words[wordIndex] & (1 << b)) != 0;
		}
		public void Set(int bitIndex)
		{
			int wordIndex = bitIndex >> 5;
			int b = bitIndex & 0x0000001F;
			words[wordIndex] |= (uint)(1 << b);
		}

		public void WriteToFile(BinaryWriter fileWriter)
		{
			fileWriter.Write(bitCount);
			foreach(uint word in words)
			{
				fileWriter.Write(word);
			}
		}

		public void ReadFromFile(BinaryReader fileReader)
		{
			bitCount = fileReader.ReadInt32();
			int wordCount = ((bitCount + 31) / 32);
			words = new uint[wordCount];

			byte[] wordsBuffer= fileReader.ReadBytes(wordCount * 4);
			Buffer.BlockCopy(wordsBuffer, 0, words, 0, wordsBuffer.Length);

			//for (int i=0;i<words.Length;i++)
			//{
			//	words[i]=fileReader.ReadUInt32();
			//}
		}
		public int GetSizeBytes()
		{
			return words.Length * sizeof(uint)+sizeof(int);
		}

		public int Count 
		{
			get { return bitCount; }
		}
		uint[] words;
		int bitCount;
	}

}
