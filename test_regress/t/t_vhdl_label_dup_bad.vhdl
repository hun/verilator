-- DESCRIPTION: Verilator: VHDL concurrent labels are unique
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (y : out integer);
end;

architecture rtl of t is
begin
  p : process begin y <= 1; wait; end process;
  p : process begin y <= 2; wait; end process;
end;
