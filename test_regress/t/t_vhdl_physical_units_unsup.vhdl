-- DESCRIPTION: Verilator: a physical unit name two physical types give different values to
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package speed_pkg is
  type SPEED is range 0 to integer'high units
    m;
    k = 1000 m;
  end units;
end package;

package data_pkg is
  type DATA is range 0 to integer'high units
    b;
    k = 1024 b;
  end units;
end package;

-- GHDL and nvc resolve `k' to SPEED from the expected type here; the frontend does
-- not know a literal's type, so it cannot tell the two packages' `k' names apart
use work.speed_pkg.all;

entity t is
  port (
    o : out integer
  );
end entity;

use work.speed_pkg.all;

architecture rtl of t is
begin
  o <= 1 k / 1 m;
end architecture rtl;
