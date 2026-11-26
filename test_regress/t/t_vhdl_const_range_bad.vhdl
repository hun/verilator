-- DESCRIPTION: Verilator: VHDL constants respect their subtype range
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A constant's value is inside its declared subtype's bounds (IEEE 1076-2008
-- 14.4.2.5); GHDL reports the violation.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  subtype small is integer range 0 to 9;
  constant k : small := 20;
begin
  y <= k;
end;
