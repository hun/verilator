-- DESCRIPTION: Verilator: a physical literal naming a unit its type does not declare
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package phys is
  type FREQ is range 0 to integer'high units
    Hz;
    kHz = 1000 Hz;
  end units;
end package;

use work.phys.all;

entity t is
  port (
    o : out integer
  );
end entity;

use work.phys.all;

architecture rtl of t is
begin
  -- The type has no unit GHz, so this literal names nothing
  o <= 1 GHz / 1 Hz;
end architecture rtl;
