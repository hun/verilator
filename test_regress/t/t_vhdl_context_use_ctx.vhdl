-- DESCRIPTION: Verilator: VHDL context declaration imports its packages
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

-- A context declaration can reference another context and bundle packages
context inner_ctx is
  library ieee;
  use ieee.numeric_std.all;
end context;

context outer_ctx is
  context work.inner_ctx;
  use work.ctx_pkg.all;
end context;

package ctx_pkg is
  constant WIDTH : natural := 4;
end package;
