-- DESCRIPTION: Verilator: VHDL illegal basic identifiers are refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Each declaration below uses an illegal basic identifier (IEEE 1076-2008
-- 15.4.2 and 15.10); GHDL refuses every one of them.

entity t is
  port (
    y : out bit
  );
end;

architecture rtl of t is
  signal a__b : bit;  -- two underscores in a row
  signal c_   : bit;  -- a trailing underscore
  signal _d   : bit;  -- a leading underscore
  signal and  : bit;  -- a reserved word
begin
  y <= '0';
end;
