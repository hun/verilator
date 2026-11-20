-- DESCRIPTION: Verilator: a VHDL package body needs a specification
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package body p is
  constant c : integer := 1;
end package body;

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  y <= 1;
end;
