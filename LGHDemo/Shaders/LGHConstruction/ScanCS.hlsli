#include "DefaultBlockSize.hlsli"

/*
	The MIT License (MIT)

	Copyright (c) 2004-2019 Microsoft Corp
	Modified by Carl Emil Carlsen 2018.

	Permission is hereby granted, free of charge, to any person obtaining a copy of this
	software and associated documentation files (the "Software"), to deal in the Software
	without restriction, including without limitation the rights to use, copy, modify,
	merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
	permit persons to whom the Software is furnished to do so, subject to the following
	conditions:

	The above copyright notice and this permission notice shall be included in all copies
	or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
	INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
	PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
	OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

	From directx-sdk-samples by Chuck Walbourn:
	https://github.com/walbourn/directx-sdk-samples/blob/master/AdaptiveTessellationCS40/ScanCS.hlsl
*/


#define groupthreads DEFAULT_BLOCK_SIZE

StructuredBuffer<uint> Input : register(t0);     // Change uint2 here if scan other types, and
RWStructuredBuffer<uint> Result : register(u0);  // also here

groupshared uint2 bucket[groupthreads];             // Change uint4 to the "type x2" if scan other types, e.g.
													// if scan uint2, then put uint4 here,
													// if scan float, then put float2 here

void CSScan(uint3 DTid, uint GI, uint x)         // Change the type of x here if scan other types
{
	// since CS40 can only support one shared memory for one shader, we use .x and .y as ping-ponging buffers
	// if scan a single element type like int, search and replace all .x to .x and .y to .y below
	bucket[GI].x = x;
	bucket[GI].y = 0;

	// Up sweep    
	[unroll]
	for (uint stride = 2; stride <= groupthreads; stride <<= 1)
	{
		GroupMemoryBarrierWithGroupSync();

		if ((GI & (stride - 1)) == (stride - 1))
		{
			bucket[GI].x += bucket[GI - stride / 2].x;
		}
	}

	if (GI == (groupthreads - 1))
	{
		bucket[GI].x = 0;
	}

	// Down sweep
	bool n = true;
	[unroll]
	for (stride = groupthreads / 2; stride >= 1; stride >>= 1)
	{
		GroupMemoryBarrierWithGroupSync();

		uint a = stride - 1;
		uint b = stride | a;

		if (n)        // ping-pong between passes
		{
			if ((GI & b) == b)
			{
				bucket[GI].y = bucket[GI - stride].x + bucket[GI].x;
			}
			else
				if ((GI & a) == a)
				{
					bucket[GI].y = bucket[GI + stride].x;
				}
				else
				{
					bucket[GI].y = bucket[GI].x;
				}
		}
		else
		{
			if ((GI & b) == b)
			{
				bucket[GI].x = bucket[GI - stride].y + bucket[GI].y;
			}
			else
				if ((GI & a) == a)
				{
					bucket[GI].x = bucket[GI + stride].y;
				}
				else
				{
					bucket[GI].x = bucket[GI].y;
				}
		}

		n = !n;
	}

	Result[DTid.x] = bucket[GI].y + x;
}