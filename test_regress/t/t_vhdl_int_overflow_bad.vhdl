-- DESCRIPTION: Verilator: VHDL static integer expressions do not overflow
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- IEEE 1076-2008 5.2.3.1: the integer type is 32 bits.

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  y <= integer'high + 1;
  y <= 2 ** 31;
  y <= 2 ** 40;
end;
