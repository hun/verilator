-- DESCRIPTION: Verilator: VHDL integer literal range
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An integer literal is inside the integer type's range (IEEE 1076-2008 5.2.3.1);
-- GHDL refuses this one, which the 32-bit model would wrap.

entity t is
  port (
    y : out integer
  );
end;

architecture rtl of t is
  constant big : integer := 4000000000;
begin
  y <= big;
end;
