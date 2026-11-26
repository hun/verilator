-- DESCRIPTION: Verilator: VHDL unconstrained numeric_std vector without an index constraint
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

use ieee.numeric_std.all;

-- The numeric_std vector type needs an index constraint at its use.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  signal x : u_unsigned;
begin
  o <= '0';
end architecture rtl;
