-- DESCRIPTION: Verilator: VHDL context declaration imports its packages
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
context work.outer_ctx;

entity t is
  port (o : out unsigned(3 downto 0));
end;

architecture rtl of t is
begin
  -- `unsigned`, `to_unsigned` and WIDTH all come from the context
  o <= to_unsigned(9, WIDTH);
end;
