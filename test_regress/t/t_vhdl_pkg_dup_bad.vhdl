-- DESCRIPTION: Verilator: VHDL duplicate package diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Two packages of one name in the design library would silently overwrite each
-- other, and the value of `k` would depend on file order. The frontend reports
-- the second unit instead.

package p is
  constant k : integer := 1;
end package;

package p is
  constant k : integer := 2;
end package;

entity t is
  port (
    y : out integer
  );
end entity;

architecture rtl of t is
  use work.p.all;
begin
  y <= k;
end architecture;
