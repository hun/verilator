-- DESCRIPTION: Verilator: VHDL disconnection specifications are refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The subset does not list disconnection specifications, so a silent skip
-- would drop a rule the hardware model needs.

entity t is
  port (y : out integer);
end;

architecture rtl of t is
  disconnect all : bit after 1 ns;
begin
  y <= 1;
end;
